/// @file
/// `OtlpProvider` over the real `BatchSpanProcessor`.

#include "tracing/otlp_provider.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "batch_env.hpp"
#include "gtest/gtest.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/trace/noop.h"
#include "recording_exporter.hpp"
#include "test_ops.hpp"
#include "watchdog.hpp"

namespace {

using namespace std::chrono_literals;
using tracing::test::kBaselineScheduleDelay;
using tracing::test::kRoomyQueue;
using tracing::test::RecordedSpans;
using tracing::test::RecordingExporter;
using tracing::test::SetBatchEnv;
using tracing::test::Watchdog;

namespace trace_sdk = opentelemetry::sdk::trace;

constexpr const char* kService = "otlp-provider-test";

/// Generous enough to not be flaky on a slow machine, but not so long that a
/// test can sit through it.
constexpr auto kWatchdogLimit = 30s;

/// @brief An exporter plus the handle onto what it recorded.
struct Exporter {
  std::unique_ptr<RecordingExporter> exporter =
      std::make_unique<RecordingExporter>();
  std::shared_ptr<RecordedSpans> data = exporter->GetData();
};

class OtlpProviderTest : public ::testing::Test {
 protected:
  void SetUp() override { SetBatchEnv(kRoomyQueue, kBaselineScheduleDelay); }

  void TearDown() override {
    trace_sdk::Provider::SetTracerProvider(
        std::make_shared<opentelemetry::trace::NoopTracerProvider>());
  }

  /// @brief Emits @p count spans named `scale`, which carry no attributes.
  static void EmitSpans(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) scale(2.0, 3.0);
  }
};

TEST_F(OtlpProviderTest, InstallsAnSdkProviderForItsLifetime) {
  Exporter sink;

  std::optional<tracing::OtlpProvider> otel;
  otel.emplace(kService, std::move(sink.exporter));

  EmitSpans(4);
  EXPECT_TRUE(otel->Flush());
  EXPECT_EQ(sink.data->Count(), 4u);

  otel.reset();
  EXPECT_TRUE(sink.data->IsShutdown());

  // The destructor restores a no-op provider rather than an empty shared_ptr,
  // so a traced call afterwards is a no-op rather than a dereferenced null.
  EmitSpans(4);
  EXPECT_EQ(sink.data->Count(), 4u);
}

TEST_F(OtlpProviderTest, ResolvesTheEndpointFromArgumentEnvAndDefault) {
  ::setenv("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT",
           "http://from-env:4318/v1/traces", 1);
  {
    tracing::OtlpProvider otel{kService, "http://explicit:4318/v1/traces"};
    EXPECT_EQ(otel.endpoint(), "http://explicit:4318/v1/traces");
  }
  {
    tracing::OtlpProvider otel{kService};
    EXPECT_EQ(otel.endpoint(), "http://from-env:4318/v1/traces");
  }

  ::unsetenv("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT");
  {
    tracing::OtlpProvider otel{kService};
    EXPECT_EQ(otel.endpoint(), tracing::kDefaultOtlpEndpoint);
  }

  // A caller-supplied exporter resolved no URL, so there is nothing to report.
  Exporter sink;
  tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
  EXPECT_TRUE(otel.endpoint().empty());
}

TEST_F(OtlpProviderTest, EmittingASpanDoesNotWaitForTheExporter) {
  Watchdog guard{kWatchdogLimit, "EmittingASpanDoesNotWaitForTheExporter"};
  Exporter sink;

  sink.data->BlockExports();

  tracing::OtlpProvider otel{kService, std::move(sink.exporter)};

  EmitSpans(8);
  EXPECT_EQ(sink.data->Count(), 0u);

  sink.data->ReleaseExports();
  EXPECT_TRUE(otel.Flush());
  EXPECT_EQ(sink.data->Count(), 8u);
}

TEST_F(OtlpProviderTest, FlushDeliversEverythingQueued) {
  Exporter sink;

  tracing::OtlpProvider otel{kService, std::move(sink.exporter)};

  EmitSpans(64);
  EXPECT_TRUE(otel.Flush());

  EXPECT_EQ(sink.data->Count(), 64u);
}

TEST_F(OtlpProviderTest, TheDestructorDrainsWithoutAFlush) {
  Exporter sink;

  {
    tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
    EmitSpans(256);
  }

  EXPECT_EQ(sink.data->Count(), 256u);
  EXPECT_TRUE(sink.data->IsShutdown());
}

TEST_F(OtlpProviderTest, FlushIsSafeWithNothingQueuedAndRepeats) {
  Exporter sink;

  tracing::OtlpProvider otel{kService, std::move(sink.exporter)};

  EXPECT_TRUE(otel.Flush());
  EXPECT_TRUE(otel.Flush());

  EmitSpans(4);
  EXPECT_TRUE(otel.Flush());
  EXPECT_EQ(sink.data->Count(), 4u);
  EXPECT_TRUE(otel.Flush());
  EXPECT_EQ(sink.data->Count(), 4u);
}

TEST_F(OtlpProviderTest, ExportedSpansCarryTheirDerivedNameAndAttributes) {
  Exporter sink;

  {
    tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
    warm_cache(16, impl::Version{2, 7}, impl::Mode::kFast);
  }

  const auto records = sink.data->Records();
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0].name, "warm_cache");
  EXPECT_EQ(records[0].attribute_keys,
            (std::vector<std::string>{"build", "mode", "slots"}));
  EXPECT_TRUE(records[0].is_root);
}

TEST_F(OtlpProviderTest, ShutdownWaitsForASlowExporter) {
  Exporter sink;

  // Small batches with a slow exporter, so the drain has several rounds of
  // real work to get through.
  SetBatchEnv(kRoomyQueue, kBaselineScheduleDelay, 32);
  sink.data->SetExportDelay(20ms);

  {
    tracing::OtlpProvider otel{kService, std::move(sink.exporter)};
    EmitSpans(256);
  }

  EXPECT_EQ(sink.data->Count(), 256u);
  EXPECT_GT(sink.data->ExportCalls(), 1u);
}

TEST_F(OtlpProviderTest, AFlushDuringConcurrentEmissionLosesNothing) {
  Exporter sink;

  constexpr std::size_t kThreads = 4;
  constexpr std::size_t kPerThread = 100;

  tracing::OtlpProvider otel{kService, std::move(sink.exporter)};

  std::atomic<bool> flushed{false};
  std::vector<std::thread> producers;
  producers.reserve(kThreads);
  for (std::size_t t = 0; t < kThreads; ++t) {
    producers.emplace_back([&] {
      for (std::size_t i = 0; i < kPerThread; ++i) {
        scale(2.0, 3.0);
        // Roughly midway, so the flush lands with work still coming.
        if (i == kPerThread / 2 && !flushed.exchange(true)) {
          EXPECT_TRUE(otel.Flush());
        }
      }
    });
  }
  for (auto& producer : producers) producer.join();

  EXPECT_TRUE(otel.Flush());
  EXPECT_EQ(sink.data->Count(), kThreads * kPerThread);
}

}  // namespace
