/// @file
/// Shutdown must not stall.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "batch_env.hpp"
#include "gtest/gtest.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/trace/noop.h"
#include "recording_exporter.hpp"
#include "test_ops.hpp"
#include "tracing/otlp_provider.hpp"
#include "watchdog.hpp"

namespace {

using namespace std::chrono_literals;
using tracing::test::kRoomyQueue;
using tracing::test::RecordedSpans;
using tracing::test::RecordingExporter;
using tracing::test::SetBatchEnv;
using tracing::test::Watchdog;

namespace trace_sdk = opentelemetry::sdk::trace;

constexpr const char* kService = "otlp-provider-wakeup-test";

/// Far longer than any test here could tolerate waiting for, which is the
/// point: nothing green below can have been delivered by the timer.
constexpr auto kUnreachableScheduleDelay = 1h;

/// A round takes much less time when the operation does not stall.
constexpr auto kWatchdogLimit = 30s;

struct Exporter {
  std::unique_ptr<RecordingExporter> exporter =
      std::make_unique<RecordingExporter>();
  std::shared_ptr<RecordedSpans> data = exporter->GetData();
};

class OtlpProviderWakeupTest : public ::testing::Test {
 protected:
  void SetUp() override { SetBatchEnv(kRoomyQueue, kUnreachableScheduleDelay); }

  void TearDown() override {
    trace_sdk::Provider::SetTracerProvider(
        std::make_shared<opentelemetry::trace::NoopTracerProvider>());
  }
};

constexpr std::size_t kRounds = 500;
constexpr std::size_t kRacingRounds = 200;

TEST_F(OtlpProviderWakeupTest, ShutdownDoesNotWaitForTheScheduleTimer) {
  Watchdog guard{kWatchdogLimit, "ShutdownDoesNotWaitForTheScheduleTimer"};

  for (std::size_t round = 0; round < kRounds; ++round) {
    Exporter sink;
    {
      tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
      scale(2.0, 3.0);
    }
    // Reaching here at all is most of the assertion: the destructor joins the
    // worker, so a missed wakeup parks it for the whole schedule delay which
    // watchdog abortes on.
    ASSERT_EQ(sink.data->Count(), 1u) << "round " << round;
  }
}

TEST_F(OtlpProviderWakeupTest, ShutdownDoesNotWaitForTheTimerWhileSpansArrive) {
  Watchdog guard{kWatchdogLimit,
                 "ShutdownDoesNotWaitForTheTimerWhileSpansArrive"};

  // A producer running across the teardown keeps the worker cycling between
  // "there is work" and "there is none".
  for (std::size_t round = 0; round < kRacingRounds; ++round) {
    Exporter sink;

    std::optional<tracing::OtlpProvider> otel;
    otel.emplace(kService, std::move(sink.exporter));

    std::atomic<bool> stop{false};
    std::thread producer{[&] {
      while (!stop.load(std::memory_order_relaxed)) {
        scale(2.0, 3.0);
        std::this_thread::sleep_for(50us);
      }
    }};

    otel.reset();
    stop.store(true, std::memory_order_relaxed);
    producer.join();

    ASSERT_TRUE(sink.data->IsShutdown()) << "round " << round;
  }
}

}  // namespace
