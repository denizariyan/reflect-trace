#pragma once

/// @file
/// The `[[=tracing::Traced{...}]]` annotation type.

namespace tracing {

/// @brief Tracing configuration attached to a function via
///        `[[= tracing::Traced{...}]]`.
///
/// @note **Optional annotation**: a traced function may be unannotated, in
/// which case the span name is derived from the function identifier. Annotate
/// only to override a default.
///
/// @warning Must be a *structural* type: every member public and non-mutable.
/// Annotation values follow the non-type template parameter rules, so a
/// non-structural member (e.g. `std::string_view`, whose `_M_len`/`_M_str` are
/// private in libstdc++) makes the annotation itself ill-formed. That is why
/// @ref name is a `const char*`.
///
/// @see detail::config_of() which reads this back off a function.
struct Traced {
  /// Span name. `nullptr` means fall back to `identifier_of(F)` (i.e., the
  /// function name).
  /// Must point at static storage, use `std::define_static_string`.
  const char* name = nullptr;
};

/// @brief Marks a single parameter for recording as a span attribute.
///
/// Recording is opt-in per parameter, because parameters are the expensive
/// part: each costs an attribute, aggregates expand into several, and whatever
/// is in them leaves the process. An unmarked parameter is never exported.
///
/// @code
///   int parse_impl([[= tracing::record]] std::string_view text,
///                  std::string_view api_key);  // unmarked: never exported
/// @endcode
///
/// @note An unmarked parameter needs no attribute mapping at all. The branch
///       that would record it is discarded before `record_value` is
///       instantiated, so a traced function may take arguments `record_value`
///       could not handle.
/// @note Unlike the `Traced` annotation, marking both a declaration and its
///       definition is harmless: this is a presence check, so two marks answer
///       the same as one.
struct Record {};
inline constexpr Record record{};

}  // namespace tracing
