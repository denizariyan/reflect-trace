#pragma once

/// @file
/// A span exporter for tests: counts what arrives, keeps enough of each span to
/// check names and parenting, and can be told to be slow or to fail.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/trace_id.h"

namespace tracing::test {

namespace trace_sdk = opentelemetry::sdk::trace;

/// @brief One exported span, flattened to the parts these tests assert on.
struct SpanRecord {
  std::string name;
  std::string trace_id;   ///< Lowercase hex.
  std::string span_id;    ///< Lowercase hex.
  bool is_root = false;   ///< I.e. the parent span id is all-zero.
  std::string parent_id;  ///< Lowercase hex; all zeroes when @ref is_root.
  std::vector<std::string> attribute_keys;  ///< Sorted.
};

/// @brief The exporter's state, shared with whoever built it.
///
/// Every accessor takes the lock, so the test thread may read while the batch
/// worker is still writing.
class RecordedSpans {
 public:
  /// @brief How many spans have reached @ref RecordingExporter::Export.
  std::size_t Count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records.size();
  }

  /// @brief How many batches, as opposed to spans, have been exported.
  std::size_t ExportCalls() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_export_calls;
  }

  /// @brief A snapshot of everything exported so far.
  ///
  /// @note Copies, and does **not** drain: unlike `InMemorySpanData::GetSpans`,
  ///       calling this twice gives the same answer twice.
  std::vector<SpanRecord> Records() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records;
  }

  /// @brief How many exported spans carry @p name.
  std::size_t CountNamed(std::string_view name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<std::size_t>(std::ranges::count_if(
        m_records, [&](const SpanRecord& r) { return r.name == name; }));
  }

  /// @brief Whether the processor has shut the exporter down.
  bool IsShutdown() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_shutdown;
  }

  /// @brief Makes every export take @p delay, simulating a slow collector.
  ///
  /// Set this before handing the exporter to a provider.
  void SetExportDelay(std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_export_delay = delay;
  }

  /// @brief Makes every export report failure. The spans are still recorded,
  ///        so a test can tell "not delivered" from "not attempted".
  void SetFailExports(bool fail) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fail_exports = fail;
  }

  /// @brief Holds every export until @ref ReleaseExports, so a test can pin
  ///        what happens while an export is in flight.
  ///
  /// @warning Release before flushing or destroying the provider: a drain
  ///          waits for the exporter, so a held export blocks shutdown.
  void BlockExports() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_block_exports = true;
  }

  /// @brief Lets held exports through, and stops holding new ones.
  void ReleaseExports() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_block_exports = false;
    m_gate.notify_all();
  }

 private:
  friend class RecordingExporter;

  std::chrono::milliseconds TakeExportDelay() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_export_delay;
  }

  /// @note Waiting releases the mutex, so the accessors above stay responsive
  ///       while an export is held.
  void WaitIfBlocked() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_gate.wait(lock, [this] { return !m_block_exports; });
  }

  bool Append(std::vector<SpanRecord> records) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_export_calls;
    m_records.insert(m_records.end(), std::make_move_iterator(records.begin()),
                     std::make_move_iterator(records.end()));
    return !m_fail_exports;
  }

  void MarkShutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdown = true;
  }

  mutable std::mutex m_mutex;
  std::condition_variable m_gate;
  std::vector<SpanRecord> m_records;
  std::size_t m_export_calls = 0;
  bool m_shutdown = false;
  std::chrono::milliseconds m_export_delay{0};
  bool m_fail_exports = false;
  bool m_block_exports = false;
};

/// @brief A `SpanExporter` that records into a @ref RecordedSpans.
class RecordingExporter final : public trace_sdk::SpanExporter {
 public:
  RecordingExporter() : m_data(std::make_shared<RecordedSpans>()) {}

  /// @brief The state to keep hold of before the provider takes the exporter.
  std::shared_ptr<RecordedSpans> GetData() const { return m_data; }

  std::unique_ptr<trace_sdk::Recordable> MakeRecordable() noexcept override {
    return std::make_unique<trace_sdk::SpanData>();
  }

  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<trace_sdk::Recordable>>&
          spans) noexcept override {
    m_data->WaitIfBlocked();

    if (const auto delay = m_data->TakeExportDelay(); delay.count() > 0) {
      std::this_thread::sleep_for(delay);
    }

    std::vector<SpanRecord> batch;
    batch.reserve(spans.size());
    for (auto& recordable : spans) {
      auto* span = static_cast<trace_sdk::SpanData*>(recordable.get());
      if (span == nullptr) continue;
      batch.push_back(Flatten(*span));
    }

    return m_data->Append(std::move(batch))
               ? opentelemetry::sdk::common::ExportResult::kSuccess
               : opentelemetry::sdk::common::ExportResult::kFailure;
  }

  bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }

  bool Shutdown(std::chrono::microseconds) noexcept override {
    m_data->MarkShutdown();
    return true;
  }

 private:
  static SpanRecord Flatten(const trace_sdk::SpanData& span) {
    SpanRecord record;
    record.name = std::string(span.GetName().data(), span.GetName().size());
    record.trace_id = ToHex(span.GetTraceId());
    record.span_id = ToHex(span.GetSpanId());
    record.is_root = !span.GetParentSpanId().IsValid();
    record.parent_id = ToHex(span.GetParentSpanId());

    for (const auto& [key, _] : span.GetAttributes()) {
      record.attribute_keys.push_back(key);
    }
    std::ranges::sort(record.attribute_keys);

    return record;
  }

  template <typename Id>
  static std::string ToHex(const Id& id) {
    char buffer[2 * Id::kSize];
    id.ToLowerBase16(buffer);
    return std::string(buffer, sizeof(buffer));
  }

  std::shared_ptr<RecordedSpans> m_data;
};

}  // namespace tracing::test
