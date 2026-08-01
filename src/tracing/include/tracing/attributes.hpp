#pragma once

/// @file
/// Turning C++ values into OpenTelemetry span attributes, recursing into
/// aggregates so nested structs flatten into dot-separated attribute keys.

#include <concepts>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/trace/span.h"
#include "tracing/detail/reflect.hpp"

namespace tracing::detail {

namespace nostd = opentelemetry::nostd;

inline bool to_attribute(bool v) { return v; }
inline double to_attribute(double v) { return v; }
inline double to_attribute(float v) { return static_cast<double>(v); }
inline const char* to_attribute(const char* v) { return v; }

inline nostd::string_view to_attribute(std::string_view v) {
  return nostd::string_view{v.data(), v.size()};
}
inline nostd::string_view to_attribute(const std::string& v) {
  return nostd::string_view{v.data(), v.size()};
}

template <std::signed_integral T>
constexpr std::int64_t to_attribute(T v) {
  return static_cast<std::int64_t>(v);
}
template <std::unsigned_integral T>
constexpr std::uint64_t to_attribute(T v) {
  return static_cast<std::uint64_t>(v);
}

/// @brief Records an enum by its enumerator *name*, recovered by reflection.
template <typename E>
  requires std::is_enum_v<E>
opentelemetry::common::AttributeValue to_attribute(E v) {
  if (const char* name = enum_name(v)) return name;
  return static_cast<std::int64_t>(v);
}

/// @brief Satisfied when @p T maps onto an `AttributeValue` alternative.
template <typename T>
concept attribute_convertible = requires(const T& v) { to_attribute(v); };

/// @brief Satisfied when @p T can be rendered with `operator<<`.
template <typename T>
concept streamable = requires(std::ostream& os, const T& v) { os << v; };

/// @brief Satisfied when @p T is an aggregate worth expanding member by member.
template <typename T>
concept expandable_aggregate =
    std::is_class_v<T> && std::is_aggregate_v<T> && (member_count<T>() > 0);

/// @brief Records one already-converted attribute under @p key.
template <typename V>
void set_attribute(opentelemetry::trace::Span& span, const std::string& key,
                   V&& value) {
  span.SetAttribute(nostd::string_view{key.data(), key.size()},
                    std::forward<V>(value));
}

/// @brief Records one attribute, recursing into aggregates.
///
/// Dispatches at compile time, in this order:
/// -# maps onto an `AttributeValue` -> recorded natively;
/// -# expandable aggregate          -> recursed into, member by member, so
///                                     nested structs flatten into dotted paths
///                                     such as `opts.limits.max_depth`;
/// -# streamable                    -> rendered to text and recorded as a
///                                     string;
/// -# otherwise                     -> hard error naming the type.
///
/// @tparam T Value type; deduced.
/// @param span Span receiving the attributes.
/// @param key  Fully qualified attribute path, e.g. `opts.limits.max_depth`.
/// @param v    Value to record.
///
/// @warning Taking @p key by value means every recursion level copies the
///          string, and the aggregate branch builds a fresh one per member.
///          Fine for a prototype, but inefficient for deeply nested aggregates.
template <typename T>
void record_value(opentelemetry::trace::Span& span, std::string key,
                  const T& v) {
  if constexpr (attribute_convertible<T>) {
    set_attribute(span, key, to_attribute(v));
  } else if constexpr (expandable_aggregate<T>) {
    template for (constexpr auto m : members_of<T>) {
      record_value(
          span,
          key + "." + std::define_static_string(std::meta::identifier_of(m)),
          v.[:m:]);
    }
  } else if constexpr (streamable<T>) {
    std::ostringstream os;
    os << v;
    const std::string rendered = os.str();
    set_attribute(span, key,
                  nostd::string_view{rendered.data(), rendered.size()});
  } else {
    static_assert(false,
                  "tracing: no attribute mapping available for this type");
  }
}

/// @brief Records the @p I -th argument of @p F, if that parameter opted in.
///
/// @tparam F Reflection of the traced function.
/// @tparam I Parameter index; indexes @ref parameter_names and
///           @ref is_parameter_recorded alike.
/// @tparam T Argument type; deduced.
/// @param span Span receiving the attribute.
/// @param v    The argument.
///
/// @note An unmarked parameter is discarded here, *before* record_value() is
///       instantiated. It therefore costs nothing at runtime and needs no
///       attribute mapping at all, so a traced function may take arguments
///       record_value() would reject.
template <std::meta::info F, std::size_t I, typename T>
void record_parameter(opentelemetry::trace::Span& span, const T& v) {
  if constexpr (is_parameter_recorded<F>[I]) {
    record_value(span, parameter_names<F>[I], v);
  }
}

}  // namespace tracing::detail
