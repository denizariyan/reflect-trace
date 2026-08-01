#pragma once

/// @file
/// SDK bootstrap: installs a tracer provider that exports over OTLP/HTTP.

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "opentelemetry/sdk/trace/tracer_provider.h"

namespace tracing {

/// Default collector endpoint: the Jaeger sidecar from
/// `.devcontainer/docker-compose.yml`.
inline constexpr const char* kDefaultOtlpEndpoint =
    "http://jaeger:4318/v1/traces";

/// @brief Installs an OTLP/HTTP tracer provider for its lifetime.
class OtlpProvider {
 public:
  /// @param service_name Value of the `service.name` resource attribute.
  ///        **Required in practice**: without it every span is filed under
  ///        `unknown_service`, and Jaeger is queried by service name.
  /// @param endpoint Full collector URL. Empty means
  ///        `$OTEL_EXPORTER_OTLP_TRACES_ENDPOINT`, else
  ///        @ref kDefaultOtlpEndpoint.
  explicit OtlpProvider(std::string_view service_name,
                        std::string_view endpoint = {});

  /// @brief Uninstalls the provider, then drains the queue and joins the
  ///        exporter's worker thread.
  ///
  /// @note Blocks until everything queued has been exported.
  ~OtlpProvider();

  OtlpProvider(const OtlpProvider&) = delete;
  OtlpProvider& operator=(const OtlpProvider&) = delete;

  /// @brief Exports everything currently queued on demand.
  ///
  /// @param timeout Upper bound on how long to block.
  /// @return True if the flush completed.
  bool Flush(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                 5000});

  /// @brief The endpoint actually in use, after env/default resolution.
  const std::string& endpoint() const { return m_endpoint; }

 private:
  /// The installed provider, kept so the destructor can shut it down explicitly
  /// rather than relying on the global reference being the last one.
  std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> m_provider;
  std::string m_endpoint;
};

}  // namespace tracing
