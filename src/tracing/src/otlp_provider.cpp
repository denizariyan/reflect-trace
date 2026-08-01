#include "tracing/otlp_provider.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <utility>

#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/noop.h"
#include "opentelemetry/trace/tracer_provider.h"

namespace tracing {
namespace {

namespace otlp = opentelemetry::exporter::otlp;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource_sdk = opentelemetry::sdk::resource;

/// @brief Resolves the collector URL: argument, then env, then the default.
std::string resolve_endpoint(std::string_view endpoint) {
  if (!endpoint.empty()) return std::string(endpoint);
  if (const char* env = std::getenv("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT")) {
    if (*env != '\0') return std::string(env);
  }
  return std::string(kDefaultOtlpEndpoint);
}

/// How long the destructor waits for the queue to drain.
constexpr std::chrono::seconds kShutdownTimeout{30};

}  // namespace

OtlpProvider::OtlpProvider(std::string_view service_name,
                           std::string_view endpoint)
    : m_endpoint(resolve_endpoint(endpoint)) {
  otlp::OtlpHttpExporterOptions options;
  options.url = m_endpoint;

  auto exporter = otlp::OtlpHttpExporterFactory::Create(options);

  trace_sdk::BatchSpanProcessorOptions batch_options{};
  auto processor = trace_sdk::BatchSpanProcessorFactory::Create(
      std::move(exporter), batch_options);

  auto resource = resource_sdk::Resource::Create(
      {{"service.name", std::string(service_name)}});

  m_provider =
      trace_sdk::TracerProviderFactory::Create(std::move(processor), resource);

  trace_sdk::Provider::SetTracerProvider(m_provider);
}

OtlpProvider::~OtlpProvider() {
  // Uninstall first, so nothing opens a span against a provider that is
  // about to shut down.
  trace_sdk::Provider::SetTracerProvider(
      std::make_shared<opentelemetry::trace::NoopTracerProvider>());

  if (m_provider) m_provider->Shutdown(kShutdownTimeout);
}

bool OtlpProvider::Flush(std::chrono::milliseconds timeout) {
  if (!m_provider) return true;
  return m_provider->ForceFlush(timeout);
}

}  // namespace tracing
