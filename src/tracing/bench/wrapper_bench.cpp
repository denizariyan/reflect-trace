/// @file
/// What a traced call actually costs, whether the wrapper shape matters, and
/// how much overhead the tracing lib adds compared to manual instrumentation.
///
///     bazel run --config=benchmark //src/tracing/bench:wrapper_bench

#include <chrono>
#include <memory>
#include <string_view>
#include <utility>

#include "bench_ops.hpp"
#include "benchmark/benchmark.h"
#include "opentelemetry/exporters/memory/in_memory_span_data.h"
#include "opentelemetry/exporters/memory/in_memory_span_exporter.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/scope.h"
#include "tracing/tracer.hpp"

namespace {

namespace trace_sdk = opentelemetry::sdk::trace;
namespace memory_exporter = opentelemetry::exporter::memory;

/// @brief Noop provider: every span is a no-op span.
///
/// @note The provider is installed once, by thread 0 only. gbench calls
///       `SetUp` on *every* thread of a multi-threaded run.
class NoProvider : public benchmark::Fixture {
 public:
  void SetUp(benchmark::State& state) override {
    if (state.thread_index() != 0) return;
    trace_sdk::Provider::SetTracerProvider(
        std::make_shared<opentelemetry::trace::NoopTracerProvider>());
  }
};

/// @brief A real tracer exporting into a bounded in-memory buffer.
///
/// `SimpleSpanProcessor`, not Batch: Batch hands off to a worker thread, so the
/// benchmark would be timing a queue push and attributing the exporter's work
/// to whichever iteration the worker happened to run under. Expect slightly
/// higher avg numbers and more contention under multi-threaded runs than a real
/// app would see.
class SdkProvider : public benchmark::Fixture {
 public:
  void SetUp(benchmark::State& state) override {
    if (state.thread_index() != 0) return;
    auto exporter = std::make_unique<memory_exporter::InMemorySpanExporter>();
    m_provider = trace_sdk::TracerProviderFactory::Create(
        trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter)));
    trace_sdk::Provider::SetTracerProvider(m_provider);
  }

  void TearDown(benchmark::State& state) override {
    if (state.thread_index() != 0) return;
    trace_sdk::Provider::SetTracerProvider(
        std::make_shared<opentelemetry::trace::NoopTracerProvider>());
    if (m_provider) m_provider->Shutdown(std::chrono::seconds{5});
    m_provider.reset();
  }

 private:
  std::shared_ptr<trace_sdk::TracerProvider> m_provider;
};

/// Varying the argument stops the compiler folding the call to a constant, and
/// `DoNotOptimize` stops it dropping the result. Without both, an -O2 build can
/// delete the entire loop body.
#define TRACE_BENCH_LOOP(expr)      \
  int n = 0;                        \
  for (auto _ : state) {            \
    benchmark::DoNotOptimize(expr); \
    ++n;                            \
  }                                 \
  state.SetItemsProcessed(state.iterations())

/// @brief Config applied to every benchmark.
void Config(benchmark::internal::Benchmark* b) {
  b->Repetitions(20)->MinTime(0.1)->DisplayAggregatesOnly(true);
}

/// @brief The baseline. What a developer writes by hand without this library.
int ManualSpan(int n) {
  auto span = tracing::tracer()->StartSpan("plain");
  opentelemetry::trace::Scope scope{span};
  span->SetAttribute("n", static_cast<int64_t>(n));
  const int result = bench::impl::raw_impl(n);
  span->End();
  return result;
}

// The shape comparison: four wrappers over four functions that do identical
// work. Any difference is the wrapper.

BENCHMARK_DEFINE_F(NoProvider, Shape_Manual)(benchmark::State& state) {
  TRACE_BENCH_LOOP(ManualSpan(n));
}
BENCHMARK_DEFINE_F(SdkProvider, Shape_Manual)(benchmark::State& state) {
  TRACE_BENCH_LOOP(ManualSpan(n));
}

BENCHMARK_DEFINE_F(NoProvider, Shape_Raw)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::impl::raw_impl(n));
}
BENCHMARK_DEFINE_F(NoProvider, Shape_TracedFn)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::plain(n));
}
BENCHMARK_DEFINE_F(NoProvider, Shape_TracedCall)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::over(n));
}
BENCHMARK_DEFINE_F(NoProvider, Shape_TracedFor)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::tpl<int>(n));
}

BENCHMARK_DEFINE_F(SdkProvider, Shape_Raw)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::impl::raw_impl(n));
}
BENCHMARK_DEFINE_F(SdkProvider, Shape_TracedFn)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::plain(n));
}
BENCHMARK_DEFINE_F(SdkProvider, Shape_TracedCall)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::over(n));
}
BENCHMARK_DEFINE_F(SdkProvider, Shape_TracedFor)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::tpl<int>(n));
}

// Parameter recording: the wrapper shape is fixed, so the delta is the cost of
// recording the parameter. The "unmarked" row is a wrapper that does not record
// its parameter, the "marked" rows record one parameter each. The "nested
// aggregate" row records three nested attributes.

BENCHMARK_DEFINE_F(SdkProvider, Payload_NoParams)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::none());
}
BENCHMARK_DEFINE_F(SdkProvider,
                   Payload_UnmarkedScalar)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::unmarked(n));
}
BENCHMARK_DEFINE_F(SdkProvider, Payload_MarkedScalar)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::plain(n));
}
BENCHMARK_DEFINE_F(SdkProvider,
                   Payload_MarkedStringView)(benchmark::State& state) {
  TRACE_BENCH_LOOP(bench::sv("orders"));
}
BENCHMARK_DEFINE_F(SdkProvider, Payload_MarkedStreamable)
(benchmark::State& state) {
  const bench::impl::Version v{"12", 345};
  TRACE_BENCH_LOOP(bench::stream(v));
}
// Above SSO threshold.
BENCHMARK_DEFINE_F(SdkProvider,
                   Payload_MarkedStreamableLong)(benchmark::State& state) {
  const bench::impl::Version v{"release-2026.08-aarch64-gcc16-opt", 345};
  TRACE_BENCH_LOOP(bench::stream(v));
}
BENCHMARK_DEFINE_F(SdkProvider,
                   Payload_NestedAggregate)(benchmark::State& state) {
  const bench::impl::Opts opts{{8, false}, 1.5};
  TRACE_BENCH_LOOP(bench::agg(opts));
}

// Decomposition of one traced call.

BENCHMARK_DEFINE_F(NoProvider, Part_TracerLookup)(benchmark::State& state) {
  for (auto _ : state) benchmark::DoNotOptimize(tracing::tracer());
}
BENCHMARK_DEFINE_F(NoProvider, Part_StartSpan)(benchmark::State& state) {
  for (auto _ : state) {
    auto span = tracing::tracer()->StartSpan("bench.span");
    benchmark::DoNotOptimize(span);
  }
}
BENCHMARK_DEFINE_F(NoProvider, Part_StartSpanAndEnd)(benchmark::State& state) {
  for (auto _ : state) {
    auto span = tracing::tracer()->StartSpan("bench.span");
    span->End();
    benchmark::DoNotOptimize(span);
  }
}
BENCHMARK_DEFINE_F(NoProvider, Part_FullSpanScope)(benchmark::State& state) {
  for (auto _ : state) {
    tracing::detail::SpanScope sp{tracing::tracer()->StartSpan("bench.span")};
    benchmark::DoNotOptimize(&sp.span());
  }
}

BENCHMARK_DEFINE_F(SdkProvider, Part_TracerLookup)(benchmark::State& state) {
  for (auto _ : state) benchmark::DoNotOptimize(tracing::tracer());
}
BENCHMARK_DEFINE_F(SdkProvider, Part_FullSpanScope)(benchmark::State& state) {
  for (auto _ : state) {
    tracing::detail::SpanScope sp{tracing::tracer()->StartSpan("bench.span")};
    benchmark::DoNotOptimize(&sp.span());
  }
}

BENCHMARK_REGISTER_F(NoProvider, Shape_Raw)->Apply(Config);
BENCHMARK_REGISTER_F(NoProvider, Shape_Manual)->Apply(Config);
BENCHMARK_REGISTER_F(NoProvider, Shape_TracedFn)->Apply(Config);
BENCHMARK_REGISTER_F(NoProvider, Shape_TracedCall)->Apply(Config);
BENCHMARK_REGISTER_F(NoProvider, Shape_TracedFor)->Apply(Config);

BENCHMARK_REGISTER_F(SdkProvider, Shape_Raw)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Shape_Manual)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Shape_TracedFn)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Shape_TracedCall)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Shape_TracedFor)->Apply(Config);

BENCHMARK_REGISTER_F(SdkProvider, Payload_NoParams)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Payload_UnmarkedScalar)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Payload_MarkedScalar)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Payload_MarkedStringView)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Payload_MarkedStreamable)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Payload_MarkedStreamableLong)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Payload_NestedAggregate)->Apply(Config);

BENCHMARK_REGISTER_F(NoProvider, Part_TracerLookup)->Apply(Config);
BENCHMARK_REGISTER_F(NoProvider, Part_StartSpan)->Apply(Config);
BENCHMARK_REGISTER_F(NoProvider, Part_StartSpanAndEnd)->Apply(Config);
BENCHMARK_REGISTER_F(NoProvider, Part_FullSpanScope)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Part_TracerLookup)->Apply(Config);
BENCHMARK_REGISTER_F(SdkProvider, Part_FullSpanScope)->Apply(Config);

}  // namespace

BENCHMARK_MAIN();
