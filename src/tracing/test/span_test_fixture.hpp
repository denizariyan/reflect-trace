#pragma once

/// @file
/// A gtest fixture that installs an SDK tracer provider exporting into memory,
/// plus accessors for reading the spans back.

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "opentelemetry/exporters/memory/in_memory_span_data.h"
#include "opentelemetry/exporters/memory/in_memory_span_exporter.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/tracer_provider.h"

namespace tracing::test {

namespace trace_sdk = opentelemetry::sdk::trace;
namespace memory_exporter = opentelemetry::exporter::memory;
namespace nostd = opentelemetry::nostd;

using SpanData = trace_sdk::SpanData;
using OwnedAttributeValue = opentelemetry::sdk::common::OwnedAttributeValue;
using AttributeMap = std::unordered_map<std::string, OwnedAttributeValue>;

/// @brief Base fixture: an in-memory tracer provider installed for one test.
///
/// @note The processor is a **SimpleSpanProcessor**, not the
///       `BatchSpanProcessor` the application uses. Batch hands off to a worker
///       thread, so a test would have to flush and could still race; Simple
///       exports inside `Span::End()`, so a span is readable the instant the
///       traced call returns.
class SpanTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto exporter = std::make_unique<memory_exporter::InMemorySpanExporter>();
    m_data = exporter->GetData();

    auto processor =
        trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
    m_provider = trace_sdk::TracerProviderFactory::Create(std::move(processor));
    trace_sdk::Provider::SetTracerProvider(m_provider);
  }

  void TearDown() override { UninstallProvider(); }

  /// @brief Uninstalls the provider mid-test, restoring a no-op one.
  ///
  /// Uninstall before shutdown, the same order as `~OtlpProvider`, so nothing
  /// opens a span against a provider that is going away. Idempotent, because
  /// TearDown() calls it too.
  ///
  /// @note Restores a `NoopTracerProvider`, not an empty `shared_ptr`.
  ///       `Provider::GetTracerProvider()` dereferences whatever it was handed,
  ///       so storing null there makes the next traced call segfault, the
  ///       same reason ~OtlpProvider does it this way.
  void UninstallProvider() {
    if (!m_provider) return;
    trace_sdk::Provider::SetTracerProvider(
        std::make_shared<opentelemetry::trace::NoopTracerProvider>());
    m_provider->Shutdown(std::chrono::seconds{1});
    m_provider.reset();
  }

  /// @brief Every span exported so far, in the order the exporter received
  /// them.
  ///
  /// @warning `InMemorySpanData::GetSpans()` **drains** the buffer: a second
  ///          call returns nothing. This accumulates instead, so the accessors
  ///          below can each call it without stealing spans from one another.
  const std::vector<std::unique_ptr<SpanData>>& Spans() {
    for (auto& span : m_data->GetSpans()) m_spans.push_back(std::move(span));
    return m_spans;
  }

  /// @brief The first span named @p name, or `nullptr`.
  const SpanData* SpanNamed(std::string_view name) {
    for (const auto& span : Spans()) {
      if (NameOf(*span) == name) return span.get();
    }
    return nullptr;
  }

  /// @brief How many exported spans carry @p name.
  std::size_t CountNamed(std::string_view name) {
    return static_cast<std::size_t>(std::ranges::count_if(
        Spans(), [&](const auto& span) { return NameOf(*span) == name; }));
  }

  /// @brief The attribute keys of the first span named @p name, sorted.
  ///
  /// Sorted because `SpanData` stores attributes in an `unordered_map`, so the
  /// only stable comparison is against a sorted expectation.
  std::vector<std::string> AttrKeys(std::string_view name) {
    std::vector<std::string> keys;
    const SpanData* span = SpanNamed(name);
    if (span == nullptr) return keys;
    for (const auto& [key, _] : span->GetAttributes()) keys.push_back(key);
    std::ranges::sort(keys);
    return keys;
  }

  /// @brief `SpanData::GetName()` as a `std::string_view`.
  static std::string_view NameOf(const SpanData& span) {
    return std::string_view{span.GetName().data(), span.GetName().size()};
  }

  /// @brief `SpanData::GetDescription()` as a `std::string_view`.
  static std::string_view DescriptionOf(const SpanData& span) {
    return std::string_view{span.GetDescription().data(),
                            span.GetDescription().size()};
  }

  /// @brief Whether parent span @p id is the all-zero span id, i.e. the span is
  /// a root.
  static bool IsRoot(const SpanData& span) {
    return !span.GetParentSpanId().IsValid();
  }

  /// @brief Asserts that @p span carries @p key holding exactly @p expected,
  ///        *in the `OwnedAttributeValue` alternative @p T*.
  template <typename T>
  static ::testing::AssertionResult HasAttr(const SpanData& span,
                                            std::string_view key,
                                            const T& expected) {
    const auto& attrs = span.GetAttributes();
    const auto it = attrs.find(std::string{key});
    if (it == attrs.end()) {
      return ::testing::AssertionFailure()
             << "no attribute '" << key << "' on span '" << NameOf(span) << "'";
    }
    if (!nostd::holds_alternative<T>(it->second)) {
      return ::testing::AssertionFailure()
             << "attribute '" << key << "' holds alternative index "
             << it->second.index() << ", not the expected one";
    }
    const T& actual = nostd::get<T>(it->second);
    if (actual != expected) {
      return ::testing::AssertionFailure()
             << "attribute '" << key << "' is "
             << ::testing::PrintToString(actual) << ", expected "
             << ::testing::PrintToString(expected);
    }
    return ::testing::AssertionSuccess();
  }

 private:
  std::shared_ptr<trace_sdk::TracerProvider> m_provider;
  std::shared_ptr<memory_exporter::InMemorySpanData> m_data;
  std::vector<std::unique_ptr<SpanData>> m_spans;
};

}  // namespace tracing::test
