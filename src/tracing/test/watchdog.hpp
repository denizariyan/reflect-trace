#pragma once

/// @file
/// An RAII deadline for a block of test code that must not get stuck.

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace tracing::test {

/// @brief Aborts the process if the guarded scope has not been left in time.
class Watchdog {
 public:
  /// @param limit How long the guarded scope may take.
  /// @param what  Named in the abort message; make it identify the scope.
  Watchdog(std::chrono::milliseconds limit, std::string what)
      : m_what(std::move(what)) {
    m_thread = std::thread([this, limit] {
      std::unique_lock<std::mutex> lock(m_mutex);
      if (m_cv.wait_for(lock, limit, [this] { return m_done; })) return;

      std::fprintf(stderr,
                   "\nwatchdog: '%s' did not finish within %lld ms, so it is "
                   "stuck; aborting\n",
                   m_what.c_str(), static_cast<long long>(limit.count()));
      std::fflush(stderr);
      std::abort();
    });
  }

  ~Watchdog() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_done = true;
      m_cv.notify_all();
    }
    m_thread.join();
  }

  Watchdog(const Watchdog&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;

 private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_done = false;
  std::string m_what;
  std::thread m_thread;
};

}  // namespace tracing::test
