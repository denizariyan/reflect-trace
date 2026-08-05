#pragma once

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace tracing::test {

/// @brief Sets the `OTEL_BSP_*` variables the batch processor reads.
///
/// @warning `setenv` is not safe against a concurrently running thread. Call
///          this before spawning any.
inline void SetBatchEnv(std::size_t max_queue_size,
                        std::chrono::milliseconds schedule_delay,
                        std::size_t max_export_batch_size = 512) {
  ::setenv("OTEL_BSP_MAX_QUEUE_SIZE", std::to_string(max_queue_size).c_str(),
           1);
  ::setenv("OTEL_BSP_SCHEDULE_DELAY",
           std::to_string(schedule_delay.count()).c_str(), 1);
  ::setenv("OTEL_BSP_MAX_EXPORT_BATCH_SIZE",
           std::to_string(max_export_batch_size).c_str(), 1);
}

/// The baseline for the tests that are about delivery rather than promptness.
inline constexpr std::chrono::milliseconds kBaselineScheduleDelay{2000};

/// Big enough that the tests cannot reasonabily fill it, so a dropped span
/// means a real loss rather than the documented behaviour of a full queue.
inline constexpr std::size_t kRoomyQueue = 65536;

}  // namespace tracing::test
