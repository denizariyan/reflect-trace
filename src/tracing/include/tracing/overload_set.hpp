#pragma once

/// @file
/// Tracing an overload set, which `TracedFn` alone cannot do due to:
///
/// 1. **`^^impl::f_impl` cannot name an overload set**, `std::meta` has no
///    overload API. The only route to an individual overload is `members_of`
///    over the enclosing namespace filtered by identifier, which is why @ref
///    traced_overloads takes a *namespace and a name*.
///
/// 2. **`TracedFn::operator()` is viable for every call**, being
///    `operator()(Args&&...)`, so merging two of them makes every call
///    ambiguous. @ref detail::TracedCall splices the real parameter types back
///    in, which restores ordinary overload resolution but costs one extra move
///    for a xvalue parameter.

#include <cstddef>
#include <exception>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "tracing/traced_fn.hpp"

namespace tracing {
namespace detail {

/// @brief A traced wrapper whose `operator()` has @p F's exact signature.
///
/// @tparam F  Reflection of the function to wrap.
/// @tparam Ps @p F's parameter types, in declaration order.
template <std::meta::info F, typename... Ps>
struct TracedCall {
  /// Annotation values, resolved once at instantiation.
  static constexpr Traced cfg = detail::config_of<F>();

  /// @brief Opens a span, records the marked parameters, and forwards to @p F.
  decltype(auto) operator()(Ps... a) const {
    return detail::run_traced<F>(
        [&](opentelemetry::trace::Span& span) {
          if constexpr (sizeof...(Ps) > 0) {
            [&]<std::size_t... I>(std::index_sequence<I...>) {
              (detail::record_parameter<F, I>(span, a...[I]), ...);
            }(std::make_index_sequence<sizeof...(Ps)>{});
          }
        },
        [&]() -> decltype(auto) { return [:F:](std::forward<Ps>(a)...); });
  }
};

/// @brief Merges N wrappers into one callable carrying all their `operator()`s,
///        so a single object serves every overload of a name.
///
/// @tparam Ws The wrappers to merge. In practice one @ref TracedCall per
///            overload, assembled by @ref overload_set_of.
template <typename... Ws>
struct Overloads : Ws... {
  using Ws::operator()...;
};

/// @brief `^^TracedCall<f, <f's first @p n parameter types>...>`.
///
/// @param f Reflection of the function.
/// @param n How many leading parameters to give the signature.
consteval std::meta::info call_wrapper(std::meta::info f, std::size_t n) {
  std::vector<std::meta::info> args{std::meta::reflect_constant(f)};
  std::size_t i = 0;
  for (auto p : std::meta::parameters_of(f)) {
    if (i++ == n) break;
    args.push_back(std::meta::type_of(p));
  }
  return std::meta::substitute(^^TracedCall, args);
}

/// @brief Index of @p f's first parameter carrying a default argument, or the
///        parameter count when none does.
///
/// C++ requires every parameter after a defaulted one to be defaulted too, so
/// this is a clean split point: parameters `[0, result)` are mandatory and
/// `[result, count]` are all valid argument numbers.
consteval std::size_t first_defaultable(std::meta::info f) {
  std::size_t i = 0;
  for (auto p : std::meta::parameters_of(f)) {
    if (std::meta::has_default_argument(p)) return i;
    ++i;
  }
  return i;
}

/// @brief One @ref TracedCall per arity (number of arguments) @p f can be
/// called with.
///
/// A spliced signature has fixed arity, so a single full-width wrapper would
/// make `f(1)` a hard error for `f(int, int = 2)`, the default argument would
/// be lost, and the failure would surface at the *callsite* as "no match for
/// call" rather than anywhere useful.
///
/// Reflection cannot recover a default argument's value: `std::meta` offers
/// `has_default_argument` as a predicate and nothing more. But it does not need
/// to. Emitting one wrapper per valid prefix length and calling `[:F:]` with
/// only that many arguments lets **the function's own defaults apply**, without
/// the wrapper ever knowing what they are.
consteval void append_call_wrappers(std::vector<std::meta::info>& out,
                                    std::meta::info f) {
  const std::size_t total = std::meta::parameters_of(f).size();
  for (std::size_t n = first_defaultable(f); n <= total; ++n) {
    out.push_back(call_wrapper(f, n));
  }
}

/// @brief Every function in @p ns whose identifier is exactly @p name.
///
/// @param ns   Reflection of the enclosing namespace.
/// @param name Identifier to collect, including any `_impl` suffix.
///
/// The match is exact string equality, so several overload sets can share a
/// namespace without interfering. Each `traced_overloads` sees only its own
/// name.
///
/// @note Function *templates* are skipped: `is_function` is false for them and
///       `parameters_of` would throw, so there is no signature to splice. A
///       traced template needs `traced_for` instead.
consteval std::vector<std::meta::info> matching_functions(
    std::meta::info ns, std::string_view name) {
  std::vector<std::meta::info> out;
  for (auto m :
       std::meta::members_of(ns, std::meta::access_context::unprivileged())) {
    if (!std::meta::is_function(m)) continue;
    if (!std::meta::has_identifier(m)) continue;
    if (std::meta::identifier_of(m) != name) continue;

    out.push_back(m);
  }
  return out;
}

/// @brief `^^Overloads<...>` over every function in @p ns named @p name.
consteval std::meta::info overload_set_of(std::meta::info ns,
                                          std::string_view name) {
  std::vector<std::meta::info> wrappers;
  for (auto m : matching_functions(ns, name)) {
    append_call_wrappers(wrappers, m);
  }
  return std::meta::substitute(^^Overloads, wrappers);
}

/// @brief The diagnostic for a @p name that matches nothing in @p ns.
consteval std::string_view no_match_message(std::meta::info ns,
                                            std::string_view name) {
  std::string s = "tracing: traced_overloads found no function named '";
  s += name;
  s += "' in namespace '";
  // Global namespace has no identifier hence the `identifier_of` throws for it.
  s +=
      std::meta::has_identifier(ns) ? std::meta::identifier_of(ns) : "(global)";
  s += "'";
  return std::define_static_string(s);
}

/// @brief Resolves @ref traced_overloads' type, rejecting an empty match for
/// proper diagnostics.
template <std::meta::info Ns, const char* Name>
struct OverloadSetFor {
  static_assert(!matching_functions(Ns, std::string_view{Name}).empty(),
                no_match_message(Ns, std::string_view{Name}));

  using type = typename[:overload_set_of(Ns, std::string_view{Name}):];
};

}  // namespace detail

/// @brief A traced wrapper over every overload of @p Name in namespace @p Ns.
///
/// @tparam Ns   Reflection of the namespace holding the overloads.
/// @tparam Name The identifier, promoted with `std::define_static_string`.
///
/// @code
///   namespace impl {
///   int parse_impl(std::string_view text);
///   int parse_impl(int value);
///   }
///   inline constexpr auto parse =
///       tracing::traced_overloads<^^impl,
///       std::define_static_string("parse_impl")>;
///
///   parse("timeout=30");   // -> span parse, the string_view overload
///   parse(30);             // -> span parse, the int overload
/// @endcode
///
/// @note A name matching nothing is a `static_assert` failure. See @ref
///       detail::OverloadSetFor.
/// @note Works for a single function too, and is then equivalent to `TracedFn`
///       except for the exact-signature trade-offs which leads to one extra
///       move for xvalue parameters. Prefer `TracedFn` unless the name really
///       is overloaded.
template <std::meta::info Ns, const char* Name>
inline constexpr
    typename detail::OverloadSetFor<Ns, Name>::type traced_overloads{};

}  // namespace tracing
