#pragma once

/// @file
/// Compile-time reflection helpers: aggregate member access, annotation
/// extraction and parameter-name derivation.

#include <algorithm>
#include <cstddef>
#include <flat_map>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "tracing/traced.hpp"

namespace tracing::detail {

/// @brief The non-static data members of @p T, promoted to static storage.
///
/// @tparam T Class type to inspect.
template <typename T>
inline constexpr auto members_of =
    std::define_static_array(std::meta::nonstatic_data_members_of(
        ^^T, std::meta::access_context::unprivileged()));

/// @brief Number of non-static data members of @p T.
///
/// A callable wrapper over @ref members_of, for use in concepts.
template <typename T>
consteval std::size_t member_count() {
  return members_of<T>.size();
}

/// @brief One row of @ref enum_table: an enumerator's value and its identifier.
template <typename E>
struct Enumerator {
  E value;
  const char* name;
};

/// @brief Every enumerator of @p E, deduplicated and sorted by value for
/// deterministic order.
///
/// @tparam E Enumeration type; scoped or unscoped.
template <typename E>
inline constexpr auto enum_table = std::define_static_array([] {
  // Dedup and sort by value
  std::flat_map<std::underlying_type_t<E>, const char*> map;
  for (auto e : std::meta::enumerators_of(^^E)) {
    map.try_emplace(
        static_cast<std::underlying_type_t<E>>(std::meta::extract<E>(e)),
        std::define_static_string(std::meta::identifier_of(e)));
  }

  std::vector<Enumerator<E>> rows;
  for (auto [value, name] : map) rows.push_back({static_cast<E>(value), name});
  return rows;
}());

/// @brief The identifier of @p v's enumerator, or `nullptr` if none matches.
///
/// @tparam E Enumeration type; scoped or unscoped.
/// @return A pointer into static storage, so it is safe to hand straight to an
///         attribute without copying.
template <typename E>
  requires std::is_enum_v<E>
constexpr const char* enum_name(E v) {
  template for (constexpr auto e : enum_table<E>) {
    if (v == e.value) return e.name;
  }
  return nullptr;
}

/// Suffix stripped from a function's identifier when deriving a default span
/// name.
///
/// This exists because C++26 cannot generate functions: the wrapper can never
/// *be* `f`, so one of the two has to carry a decorated name or exist in a diff
/// namespace. The `_impl` suffix is the convention this library uses.
inline constexpr std::string_view kImplSuffix = "_impl";

/// @brief The identifier to derive a span name from, for a plain function *or*
///        for a specialisation of a function template.
///
/// @param f Reflection of a function or of a function-template specialisation.
consteval std::string_view identifier_for(std::meta::info f) {
  return std::meta::has_identifier(f)
             ? std::meta::identifier_of(f)
             : std::meta::identifier_of(std::meta::template_of(f));
}

/// @brief Extracts the `Traced` annotation from @p F, with defaults filled in.
///
/// @tparam F Reflection of the function..
/// @return The annotation's value, or a default-constructed `Traced` if the
///         function carries none. `name` is never null on return.
///
/// @note gcc unions annotations across declarations. Annotating both a
///       declaration and its definition therefore yields two, and this loop
///       keeps the last one seen, so annotate exactly one of them.
template <std::meta::info F>
consteval Traced config_of() {
  Traced config{};
  // `annotations_of` throws on a function *template* as a template is not a
  // function.
  //
  // Concretely, for a template carrying `.name = "json.render"`, the two ways
  // of wrapping it disagree:
  //
  //     TracedFn<^^tpl>                 -> span "render_json"  annotation
  //                                        skipped; name from the identifier
  //     traced_for<^^tpl, std::string>  -> span "json.render"  annotation
  //                                        honoured
  //
  // The annotation is not unreadable in general. It lives on the template
  // declaration and is visible through *any* specialisation, which is exactly
  // what `traced_for` builds with `substitute`.
  //
  // So put a `.name` on a template only if callers reach it through
  // `traced_for`. A bare `TracedFn<^^tpl>` ignores it.
  if constexpr (std::meta::is_function(F)) {
    for (auto ann : std::meta::annotations_of(F)) {
      // Annotation values are constants:
      // type_of(ann) is `const Traced`, NOT `Traced`.
      if (std::meta::remove_cv(std::meta::type_of(ann)) == ^^Traced) {
        config = std::meta::extract<Traced>(ann);
      }
    }
  }

  // Use function name as default
  if (config.name == nullptr) {
    std::string_view id = identifier_for(F);
    // Don't strip it if the func is just called `_impl`.
    if (id.size() > kImplSuffix.size() && id.ends_with(kImplSuffix)) {
      id.remove_suffix(kImplSuffix.size());
    }
    config.name = std::define_static_string(id);
  }
  return config;
}

/// @brief The placeholder name for the @p i -th parameter: `arg0`, `arg1`, ...
///
/// @note Required because `std::to_string` is not constexpr.
consteval std::string arg_placeholder(std::size_t i) {
  std::string digits;
  do {
    digits.push_back(static_cast<char>('0' + (i % 10)));
    i /= 10;
  } while (i > 0);
  std::ranges::reverse(digits);
  return "arg" + digits;
}

/// @brief The parameter names of @p F, in declaration order.
///
/// @tparam F Reflection of the function.
template <std::meta::info F>
inline constexpr auto parameter_names = std::define_static_array([] {
  std::vector<const char*> v;
  std::size_t i = 0;
  for (auto p : std::meta::parameters_of(F)) {
    /// Parameters have *no* identifier when a declaration and the definition
    /// spell them differently. `identifier_of` would then throw at
    /// instantiation, so this falls back to `arg0`, `arg1`, ... instead of
    /// failing the build.
    v.push_back(std::meta::has_identifier(p)
                    ? std::define_static_string(std::meta::identifier_of(p))
                    : std::define_static_string(arg_placeholder(i)));
    ++i;
  }
  return v;
}());

/// @brief Whether each parameter of @p F carries `[[= tracing::record]]`, in
///        declaration order.
///
/// @tparam F Reflection of the function.
///
/// @return A static array of booleans indicating whether that parameter should
///         be recorded.
template <std::meta::info F>
inline constexpr auto is_parameter_recorded = std::define_static_array([] {
  std::vector<bool> v;
  for (auto p : std::meta::parameters_of(F)) {
    bool should_record = false;
    for (auto ann : std::meta::annotations_of(p)) {
      if (std::meta::remove_cv(std::meta::type_of(ann)) == ^^Record) {
        should_record = true;
      }
    }
    v.push_back(should_record);
  }
  return v;
}());

/// @brief Diagnostic: the type of every annotation on @p F, as a printable
///        string.
///
/// @tparam F Reflection of the entity to inspect.
template <std::meta::info F>
inline constexpr const char* annotation_of = std::define_static_string([] {
  std::string s;
  for (auto a : std::meta::annotations_of(F)) {
    s += std::string(std::meta::display_string_of(std::meta::type_of(a))) +
         " | ";
  }
  return s.empty() ? std::string("(no annotations)") : s;
}());

}  // namespace tracing::detail
