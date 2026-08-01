#pragma once

/// @file
/// Access to the ambient tracer, and the RAII span scope the wrapper uses.

#include <utility>

#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/tracer_provider.h"

namespace tracing {

/// Instrumentation scope name reported for every span this library opens.
inline constexpr const char* kInstrumentationScope = "reflect_trace/tracing";
/// Instrumentation scope version reported alongside @ref kInstrumentationScope.
inline constexpr const char* kInstrumentationVersion = "0.1";

/// @brief The tracer all wrappers emit through.
///
/// @return A tracer from whichever provider is currently installed. Before a
///         provider is installed this is a no-op tracer.
inline opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer() {
  return opentelemetry::trace::Provider::GetTracerProvider()->GetTracer(
      kInstrumentationScope, kInstrumentationVersion);
}

namespace detail {

/// @brief RAII wrapper to own a span for the duration of a scope.
class SpanScope {
 public:
  /// @param span Span to own; made the active span for this scope.
  explicit SpanScope(
      opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span)
      : m_span(std::move(span)), m_scope(m_span) {}

  ~SpanScope() { m_span->End(); }

  SpanScope(const SpanScope&) = delete;
  SpanScope& operator=(const SpanScope&) = delete;

  /// @brief The owned span, for recording attributes and status.
  opentelemetry::trace::Span& span() { return *m_span; }

 private:
  // m_span is declared first so that m_scope, which refers to it, is destroyed
  // first.
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> m_span;
  opentelemetry::trace::Scope m_scope;
};

}  // namespace detail
}  // namespace tracing
