#pragma once

/// @file
/// The callable wrapper that turns an annotated function into a traced one.

#include <cstddef>
#include <exception>
#include <meta>
#include <type_traits>
#include <utility>

#include "opentelemetry/trace/span_metadata.h"
#include "tracing/attributes.hpp"
#include "tracing/detail/reflect.hpp"
#include "tracing/traced.hpp"
#include "tracing/tracer.hpp"

namespace tracing {
namespace detail {

/// @brief The body every traced wrapper shares.
///
/// Opens a span named after @p F, lets @p record put attributes on it, invokes
/// @p invoke, and maps an escaping exception onto the span status.
///
/// @tparam F      Reflection of the wrapped function.
/// @param  record Called with the span; records whichever parameters opted in.
/// @param  invoke Called to perform the wrapped call.
/// @return Whatever @p invoke returns, preserving value category.
template <std::meta::info F, typename Record, typename Invoke>
decltype(auto) run_traced(Record&& record, Invoke&& invoke) {
  static constexpr Traced kConfig = config_of<F>();

  SpanScope sp{tracer()->StartSpan(kConfig.name)};
  record(sp.span());

  try {
    using Result = decltype(invoke());
    if constexpr (std::is_void_v<Result>) {
      invoke();
    } else {
      return invoke();
    }
  } catch (const std::exception& e) {
    sp.span().SetStatus(opentelemetry::trace::StatusCode::kError, e.what());
    throw;
  } catch (...) {
    sp.span().SetStatus(opentelemetry::trace::StatusCode::kError,
                        "unknown exception");
    throw;
  }
}

}  // namespace detail

/// @brief Callable wrapper that opens a span around the function @p F.
///
/// Instantiating this type resolves the annotation, the span name and the
/// parameter names to constants in compile time.
///
/// @code
///   namespace impl {
///   [[= tracing::Traced{.name = std::define_static_string("cfg.parse")}]]
///   int parse_impl([[= tracing::record]] std::string_view text, bool strict);
///   }
///   inline constexpr tracing::TracedFn<^^impl::parse_impl> parse{};
/// @endcode
///
/// @tparam F Reflection of the function to wrap.
///
/// @note This is an object with `operator()`, not a function, because C++26
///       reflection cannot generate functions. Consequences: `&parse` is not a
///       function pointer, ADL will not find it, and each overload needs its
///       own instantiation.
/// @note The `Traced` annotation is **optional**: wrapping @p F is what opts it
///       into tracing, and an unannotated function gets its span name derived
///       from its identifier just the same. Annotate only to override the name.
/// @note If you do annotate, annotate *either* the declaration or the
///       definition, never both. gcc unions annotations across declarations and
///       config_of() then silently keeps the last. Parameter names must also
///       match between the two, or gcc leaves them with no identifier at all
///       and the names degrade to `arg0`, `arg1`, ...
template <std::meta::info F>
struct TracedFn {
  /// Annotation values, resolved once at instantiation.
  static constexpr Traced cfg = detail::config_of<F>();

  /// @brief Opens a span, records the parameters, and forwards to @p F.
  ///
  /// @tparam Args Deduced argument types.
  /// @param a Arguments, perfectly forwarded to @p F.
  /// @return Whatever @p F returns, preserving value category.
  ///
  /// @note An escaping exception marks the span as errored and is rethrown
  ///       unchanged.
  template <typename... Args>
  decltype(auto) operator()(Args&&... a) const {
    return detail::run_traced<F>(
        [&](opentelemetry::trace::Span& span) {
          // F may be a function *template*, which is not a function:
          // `parameters_of` throws "reflection does not represent a function or
          // function type" on one, so there are no names and no marks to work
          // with. Such a wrapper still forwards correctly -- the splice below
          // defers deduction to the callsite -- so it degrades to a named span
          // with no attributes rather than failing to compile. Use
          // traced_for<Tpl, Ts...> to trace a specific instantiation with its
          // parameters.
          if constexpr (std::meta::is_function(F)) {
            // Pack indexing gives every argument a *constexpr* index, which is
            // what lets record_parameter() discard an unmarked parameter at
            // compile time.
            //
            // The sizeof... guard is NOT redundant. With no arguments the fold
            // expands to nothing, so `a...[I]` is never evaluated but gcc 16.1
            // rejects it anyway at instantiation.
            if constexpr (sizeof...(Args) > 0) {
              [&]<std::size_t... I>(std::index_sequence<I...>) {
                (detail::record_parameter<F, I>(span, a...[I]), ...);
              }(std::index_sequence_for<Args...>{});
            }
          }
        },
        [&]() -> decltype(auto) { return [:F:](std::forward<Args>(a)...); });
  }
};

/// @brief A traced wrapper over one instantiation of a function template.
///
/// @tparam Tpl Reflection of the function template.
/// @tparam Ts  Template arguments to instantiate it with.
///
/// @code
///   namespace impl {
///   template <typename T>
///   std::string render_json_impl(const T& body,
///                                [[= tracing::record]] std::string_view cs);
///   }
///   template <typename T>
///   inline constexpr auto render_json =
///       tracing::traced_for<^^impl::render_json_impl, T>;
///
///   render_json<std::vector<Row>>(rows, "utf-8");
/// @endcode
///
/// Unlike a bare `TracedFn<^^tpl>`, this records parameters: `substitute`
/// yields a real function reflection, and parameter names, `[[=
/// tracing::record]]` marks and substituted types all survive it. Those are
/// properties of the *template's declaration*, so they are identical across
/// instantiations. Annotate the template once and every instantiation picks
/// it up.
///
/// @note Instantiated on demand. In generic code `Ts` are the caller's own
///       template parameters, so nothing has to be enumerated up front.
/// @note There is **no deduction**: `Ts` must be spelled. C++26 has no query
///       that inverts template argument deduction, and none that reflects the
///       specialisation an expression selected.
/// @note Type arguments only. For a template with a non-type parameter, build
///       the reflection directly:
///       `TracedFn<std::meta::substitute(^^impl::pick_impl,
///                                       {^^double,
///                                       std::meta::reflect_constant(3)})>`.
template <std::meta::info Tpl, typename... Ts>
inline constexpr TracedFn<std::meta::substitute(Tpl, {^^Ts...})> traced_for{};

}  // namespace tracing
