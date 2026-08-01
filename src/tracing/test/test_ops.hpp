#pragma once

/// @file
/// The traced surface the unit tests exercise: the shapes worth recording, the
/// annotated declarations, and the wrappers the tests call.

#include <memory>
#include <meta>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "tracing/tracing.hpp"

namespace impl {

/// The enum that actually gets recorded.
enum class Mode { kFast = 1, kSafe = 2 };

/// Declared out of order, so a sorted @ref tracing::detail::enum_table is
/// distinguishable from one in declaration order.
enum class Reordered { kThree = 3, kOne = 1, kTwo = 2 };

/// Two enumerators share a value. `flat_map::try_emplace` keeps the first
/// inserted, and `enumerators_of` yields declaration order, so `kOne` wins.
enum class Aliased { kOne = 1, kUno = 1, kTwo = 2 };

/// Unscoped, so the table must not depend on scoped-enum syntax.
enum Unscoped { kAlpha = 10, kBeta = 20 };

/// Negative values and a non-`int` underlying type, so sorting is on the
/// underlying type rather than on something that assumes unsigned.
enum class Signed : short { kNeg = -5, kZero = 0, kPos = 7 };

/// Inner aggregate, so expansion has two levels to walk.
struct Limits {
  int max_depth;
  bool allow_dupes;
};

/// Outer aggregate; expands to `opts.limits.max_depth`, `opts.timeout_s`, ...
struct Opts {
  Limits limits;
  double timeout_s;
  Mode mode;
};

/// Not an aggregate (private members) and not convertible to an
/// `AttributeValue`, but streamable.
class Version {
 public:
  Version(int major, int minor) : m_major(major), m_minor(minor) {}

  friend std::ostream& operator<<(std::ostream& os, const Version& v) {
    return os << v.m_major << '.' << v.m_minor;
  }

 private:
  int m_major;
  int m_minor;
};

/// Neither an aggregate (private member) nor streamable nor convertible, so
/// record_value() would reject it with its static_assert.
class Credentials {
 public:
  explicit Credentials(std::string token) : m_token(std::move(token)) {}

 private:
  std::string m_token;
};

/// Counts its own copies and moves, so a test can assert on overhead or the
/// lack thereof the wrapper's return path adds.
struct Counted {
  static inline int copies = 0;
  static inline int moves = 0;

  int value = 0;

  Counted() = default;
  explicit Counted(int v) : value(v) {}
  Counted(const Counted& o) : value(o.value) { ++copies; }
  Counted(Counted&& o) noexcept : value(o.value) { ++moves; }
  Counted& operator=(const Counted&) = delete;
  Counted& operator=(Counted&&) = delete;

  static void Reset() {
    copies = 0;
    moves = 0;
  }
};

/// @brief Every scalar family that tracing lib handles, each marked, so a
///        test can pin which `OwnedAttributeValue` alternative each lands in.
void scalars_impl([[= tracing::record]] bool flag,
                  [[= tracing::record]] int signed_v,
                  [[= tracing::record]] unsigned unsigned_v,
                  [[= tracing::record]] double double_v,
                  [[= tracing::record]] float float_v,
                  [[= tracing::record]] std::string_view view_v,
                  [[= tracing::record]] std::string string_v,
                  [[= tracing::record]] const char* cstr_v);

/// @brief Custom span name and a nested aggregate parameter.
/// @param opts Expanded into `opts.limits.max_depth`, `opts.timeout_s`, ...
[[= tracing::Traced{.name = std::define_static_string("cfg.parse")}]] int
    parse_impl([[= tracing::record]] std::string_view text,
               [[= tracing::record]] bool strict,
               [[= tracing::record]] Opts opts);

/// @brief Void return.
/// @param build Streamable non-aggregate; recorded as the text "2.7".
/// @param mode  Called with a value that has no enumerator, so it exercises
///              to_attribute()'s fall back to the underlying integer.
void warm_cache_impl([[= tracing::record]] int slots,
                     [[= tracing::record]] Version build,
                     [[= tracing::record]] Mode mode);

/// @brief Neither parameter marked: the span exists and carries no attributes.
double scale_impl(double x, double factor);

/// @brief A marked parameter beside an unmarked one whose type record_value()
///        could not handle at all.
int handle_request_impl([[= tracing::record]] std::string_view route,
                        const Credentials& creds);

/// @brief Throws a `std::runtime_error`, for testing exception handling and the
///        span status.
[[= tracing::Traced{.name = std::define_static_string("cfg.reload")}]] void
    reload_impl(std::string_view path);

/// @brief Throws an `int`, for the `catch (...)` arm.
void throw_int_impl();

/// @brief Top of a three-deep chain: calls `level2`, which calls `level3`.
int level1_impl();
int level2_impl();
int level3_impl();

/// @brief Calls two children in sequence, so both must be siblings under it
///        rather than the second nesting inside the first.
void two_children_impl();
void child_a_impl();
void child_b_impl();

/// @brief Returns an lvalue reference, so `decltype(auto)` has something to
///        preserve where auto would decay to a value.
int& ref_slot_impl();

/// @brief Returns by value; used with @ref Counted to check the return path.
Counted make_counted_impl(int v);

/// @brief Takes a move-only parameter, unmarked. Records nothing, and must
///        still receive the pointer rather than a copy.
int consume_impl(std::unique_ptr<int> p);

/// @brief Mutates its argument, so a test can prove an lvalue stays an lvalue
///        across the wrapper.
void bump_impl(int& n);

/// @brief No parameters, void return.
void nothing_impl();

/// @brief A traced function template.
template <typename T>
std::string render_json_impl(const T& body,
                             [[= tracing::record]] std::string_view charset) {
  return std::string{charset} + ":" + std::to_string(body.size());
}

/// @brief A template carrying an explicit `.name`, to pin the one place the two
///        template wrappers disagree.
///
/// `traced_for` honours the name; a bare `TracedFn<^^tpl>` falls back to the
/// identifier, because `annotations_of` throws on a template and there is no
/// specialisation to read it from. Declaration-only, nothing calls it.
template <typename T>
[[= tracing::Traced{.name = std::define_static_string("custom.tpl")}]] T
    named_tpl_impl(T v);

/// @brief A template with a non-type parameter.
template <typename T, int N>
T bump_by_impl([[= tracing::record]] T value) {
  return value + N;
}

/// @brief Overload taking a string; returns its size.
int pick_impl([[= tracing::record]] std::string_view s);
/// @brief Overload taking an int; returns it doubled.
int pick_impl([[= tracing::record]] int n);
/// @brief Two-parameter overload, so arity varies across the set.
int pick_impl([[= tracing::record]] int n, [[= tracing::record]] bool twice);

/// @brief Overload set of one, to pin that `traced_overloads` generates
///        correctly rather than needing a second member.
int only_one_impl([[= tracing::record]] int n);

/// @brief Default arguments through the exact-signature path.
///
/// A spliced signature has fixed arity, so without one wrapper per call arity
/// `defaulted(1)` would be a hard error at the callsite. Reflection cannot read
/// what `= 2` and `= 3` *are*; it only needs `has_default_argument` to know how
/// many arities exist, and then lets the real function supply the values.
int defaulted_impl([[= tracing::record]] int a, [[= tracing::record]] int b = 2,
                   [[= tracing::record]] int c = 3);
/// A second overload, so this is genuinely an overload set and not a single
/// function that can use perfect forwarding.
int defaulted_impl([[= tracing::record]] std::string_view s);

/// @brief A function with no arguments, so the wrapper's `operator()` has no
///        parameters to forward.
int no_args_impl();

/// @brief Returns an lvalue reference through the exact-signature wrapper, so a
///        test can prove it preserves lvalueness where a bare auto return type
///        would decay to a value.
int& ref_slot_ovl_impl();

/// @brief Takes a move-only parameter by value through the exact-signature
///        wrapper.
int consume_ovl_impl(std::unique_ptr<int> p);

/// @brief Takes @ref Counted by value, so a test can count the moves the
///        exact-signature wrapper actually costs. Part of an overload set so it
///        routes through TracedCall rather than TracedFn.
int count_moves_impl(Counted c);
int count_moves_impl(int n);

/// @brief The `TracedFn` counterpart, same by-value `Counted` parameter, so the
///        two wrappers can be compared directly.
int count_moves_fn_impl(Counted c);

/// @brief Takes @ref Counted by *reference*, in an overload set so it routes
///        through TracedCall.
int by_ref_ovl_impl(const Counted& c);
int by_ref_ovl_impl(int n);

/// @brief The documented degradation: the definition in test_ops.cpp spells
///        these parameters `alpha`/`beta`, which gcc refuses to reconcile.
/// @note The mark survives the mismatch, the lost identifier and the
///       annotation are independent, so both are still recorded.
/// @see RenamedNamesFromDefinitionTu(), which is how the test observes that the
///      degradation is per translation unit.
int renamed_impl([[= tracing::record]] int first,
                 [[= tracing::record]] int second);

/// @brief `parameter_names<^^renamed_impl>` as resolved *inside test_ops.cpp*,
///        the one TU where both the declaration and the definition are visible.
///
/// A TU that has only seen the header resolves the declaration's names
/// normally; only where the two spellings collide does gcc drop the identifier.
/// Returning the result across the boundary is the only way a test can compare
/// the two.
std::pair<const char*, const char*> RenamedNamesFromDefinitionTu();

namespace naming {

/// `_impl` stripped.
[[= tracing::Traced{}]] void plain_impl();
/// No suffix to strip.
[[= tracing::Traced{}]] void bare();
/// Stripping would leave an empty span name.
[[= tracing::Traced{}]] void _impl();
/// `ends_with`, not "contains", so this keeps its whole name.
[[= tracing::Traced{}]] void implicit();
/// Contains the literal `_impl`, just not at the end.
void _impl_detail();
/// An explicit name beats both the identifier and the suffix rule.
[[= tracing::Traced{
    .name = std::define_static_string("custom.name")}]] void named_impl();
[[= tracing::Traced{.name = std::define_static_string(
                        "keep_impl")}]] void explicit_suffix_impl();
/// No annotation at all.
void unannotated_impl(int a, int b);

}  // namespace naming

/// Empty aggregate.
struct Empty {};

}  // namespace impl

/// Traced entry points the tests call.
inline constexpr tracing::TracedFn<^^impl::scalars_impl> scalars{};
inline constexpr tracing::TracedFn<^^impl::parse_impl> parse{};
inline constexpr tracing::TracedFn<^^impl::warm_cache_impl> warm_cache{};
inline constexpr tracing::TracedFn<^^impl::scale_impl> scale{};
inline constexpr tracing::TracedFn<^^impl::handle_request_impl>
    handle_request{};
inline constexpr tracing::TracedFn<^^impl::reload_impl> reload{};
inline constexpr tracing::TracedFn<^^impl::throw_int_impl> throw_int{};
inline constexpr tracing::TracedFn<^^impl::level1_impl> level1{};
inline constexpr tracing::TracedFn<^^impl::level2_impl> level2{};
inline constexpr tracing::TracedFn<^^impl::level3_impl> level3{};
inline constexpr tracing::TracedFn<^^impl::two_children_impl> two_children{};
inline constexpr tracing::TracedFn<^^impl::child_a_impl> child_a{};
inline constexpr tracing::TracedFn<^^impl::child_b_impl> child_b{};
inline constexpr tracing::TracedFn<^^impl::ref_slot_impl> ref_slot{};
inline constexpr tracing::TracedFn<^^impl::make_counted_impl> make_counted{};
inline constexpr tracing::TracedFn<^^impl::consume_impl> consume{};
inline constexpr tracing::TracedFn<^^impl::bump_impl> bump{};
inline constexpr tracing::TracedFn<^^impl::nothing_impl> nothing{};
inline constexpr tracing::TracedFn<^^impl::renamed_impl> renamed{};

/// One instantiation of a template, with full parameter metadata. Instantiated
/// on demand: `T` may be a caller's own template parameter.
template <typename T>
inline constexpr auto render_json =
    tracing::traced_for<^^impl::render_json_impl, T>;

/// The same template wrapped bare: deduces `T` at the callsite, but records no
/// parameters.
inline constexpr tracing::TracedFn<^^impl::render_json_impl> render_any{};

/// A non-type template parameter, which `traced_for` cannot express.
inline constexpr tracing::TracedFn<std::meta::substitute(
    ^^impl::bump_by_impl,
    {
        ^^int, std::meta::reflect_constant(5)})>
    bump_by_5{};

/// Overload sets.
inline constexpr auto pick =
    tracing::traced_overloads<^^impl, std::define_static_string("pick_impl")>;
inline constexpr auto only_one =
    tracing::traced_overloads<^^impl,
                              std::define_static_string("only_one_impl")>;
inline constexpr auto no_args =
    tracing::traced_overloads<^^impl,
                              std::define_static_string("no_args_impl")>;
inline constexpr auto ref_slot_ovl =
    tracing::traced_overloads<^^impl,
                              std::define_static_string("ref_slot_ovl_impl")>;
inline constexpr auto consume_ovl =
    tracing::traced_overloads<^^impl,
                              std::define_static_string("consume_ovl_impl")>;
inline constexpr auto count_moves =
    tracing::traced_overloads<^^impl,
                              std::define_static_string("count_moves_impl")>;
inline constexpr auto by_ref_ovl =
    tracing::traced_overloads<^^impl,
                              std::define_static_string("by_ref_ovl_impl")>;
inline constexpr auto defaulted =
    tracing::traced_overloads<^^impl,
                              std::define_static_string("defaulted_impl")>;
inline constexpr tracing::TracedFn<^^impl::count_moves_fn_impl>
    count_moves_fn{};
