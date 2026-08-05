/// @file
/// `OtlpProvider` under load: many producers, repeated flushes, repeated
/// shutdowns.
///
/// Similar subjects as `otlp_provider_test`, but under more stress.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "batch_env.hpp"
#include "gtest/gtest.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/trace/noop.h"
#include "recording_exporter.hpp"
#include "test_ops.hpp"
#include "tracing/otlp_provider.hpp"
#include "watchdog.hpp"

namespace {

using namespace std::chrono_literals;
using tracing::test::kBaselineScheduleDelay;
using tracing::test::kRoomyQueue;
using tracing::test::RecordedSpans;
using tracing::test::RecordingExporter;
using tracing::test::SetBatchEnv;
using tracing::test::SpanRecord;
using tracing::test::Watchdog;

namespace trace_sdk = opentelemetry::sdk::trace;

constexpr const char* kService = "otlp-provider-stress-test";

/// Generous on purpose: it should say "wedged", not "slow machine".
constexpr auto kWatchdogLimit = 60s;

struct Exporter {
  std::unique_ptr<RecordingExporter> exporter =
      std::make_unique<RecordingExporter>();
  std::shared_ptr<RecordedSpans> data = exporter->GetData();
};

class OtlpProviderStressTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetBatchEnv(kRoomyQueue, kBaselineScheduleDelay);

    // A dropped span is logged one line per span at warning level. The suite
    // below overflows a queue on purpose, so left alone that is tens of
    // thousands of lines of non interesting logs otherwise.
    namespace internal_log = opentelemetry::sdk::common::internal_log;
    internal_log::GlobalLogHandler::SetLogLevel(internal_log::LogLevel::Error);
  }

  void TearDown() override {
    trace_sdk::Provider::SetTracerProvider(
        std::make_shared<opentelemetry::trace::NoopTracerProvider>());
  }

  /// @brief Runs @p body on @p count threads and joins them all.
  template <typename Body>
  static void InParallel(std::size_t count, Body body) {
    std::vector<std::thread> threads;
    threads.reserve(count);
    for (std::size_t t = 0; t < count; ++t) threads.emplace_back(body, t);
    for (auto& thread : threads) thread.join();
  }
};

TEST_F(OtlpProviderStressTest, ManyThreadsEmittingConcurrentlyLoseNothing) {
  Watchdog guard{kWatchdogLimit, "ManyThreadsEmittingConcurrentlyLoseNothing"};
  Exporter sink;

  constexpr std::size_t kThreads = 8;
  constexpr std::size_t kPerThread = 2000;
  static_assert(
      kThreads * kPerThread < kRoomyQueue,
      "the queue must not be able to fill, or a loss here would be expected");

  {
    tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
    InParallel(kThreads, [](std::size_t) {
      for (std::size_t i = 0; i < kPerThread; ++i) scale(2.0, 3.0);
    });
  }

  EXPECT_EQ(sink.data->Count(), kThreads * kPerThread);
  EXPECT_TRUE(sink.data->IsShutdown());
}

TEST_F(OtlpProviderStressTest, RepeatedConstructAndDestroyDeliversEveryRound) {
  Watchdog guard{kWatchdogLimit,
                 "RepeatedConstructAndDestroyDeliversEveryRound"};

  constexpr std::size_t kRounds = 200;

  for (std::size_t round = 0; round < kRounds; ++round) {
    Exporter sink;
    {
      tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
      scale(2.0, 3.0);
    }
    ASSERT_EQ(sink.data->Count(), 1u) << "round " << round << " lost its span";
  }
}

TEST_F(OtlpProviderStressTest, ShutdownWhileProducersAreStillEnqueuing) {
  Watchdog guard{kWatchdogLimit, "ShutdownWhileProducersAreStillEnqueuing"};

  constexpr std::size_t kRounds = 50;
  constexpr std::size_t kProducers = 4;

  for (std::size_t round = 0; round < kRounds; ++round) {
    Exporter sink;

    std::optional<tracing::OtlpProvider> otel;
    otel.emplace(kService, std::move(sink.exporter));

    std::atomic<bool> stop{false};
    std::atomic<std::size_t> emitted{0};
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (std::size_t t = 0; t < kProducers; ++t) {
      producers.emplace_back([&] {
        while (!stop.load(std::memory_order_relaxed)) {
          scale(2.0, 3.0);
          emitted.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    // Destroy the provider out from under them.
    std::this_thread::sleep_for(1ms);
    otel.reset();

    stop.store(true, std::memory_order_relaxed);
    for (auto& producer : producers) producer.join();

    // Only an upper bound is available: a span opened after the swap goes to
    // the no-op provider and is not exported. What matters is that the
    // teardown completed, exported what it had, and did not blow up.
    EXPECT_TRUE(sink.data->IsShutdown()) << "round " << round;
    EXPECT_LE(sink.data->Count(), emitted.load()) << "round " << round;
  }
}

TEST_F(OtlpProviderStressTest, AFullQueueDropsSpansWithoutHangingOrCrashing) {
  Watchdog guard{kWatchdogLimit,
                 "AFullQueueDropsSpansWithoutHangingOrCrashing"};
  Exporter sink;

  constexpr std::size_t kEmitted = 4000;

  // A queue that cannot hold the load, drained by an exporter too slow to keep
  // up. There is no backpressure: `OnEnd` returns whether or not there was
  // room.
  SetBatchEnv(64, kBaselineScheduleDelay, 32);
  sink.data->SetExportDelay(50ms);

  {
    tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
    for (std::size_t i = 0; i < kEmitted; ++i) scale(2.0, 3.0);
  }

  const std::size_t exported = sink.data->Count();
  EXPECT_GT(exported, 0u) << "nothing got through at all";
  EXPECT_LT(exported, kEmitted)
      << "the queue was meant to overflow; if it did not, this test is no "
         "longer exercising the drop path";
  EXPECT_TRUE(sink.data->IsShutdown());
}

}  // namespace
