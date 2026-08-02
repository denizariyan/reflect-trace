#pragma once

/// @file
/// Subjects for the wrapper microbenchmarks.
///
/// Every callee here does the trivial work and every one of them is defined out
/// of line in bench_ops.cpp to prevent optimiser from collapsing the four
/// shapes into one.

#include <memory>
#include <meta>
#include <ostream>
#include <string>
#include <string_view>

#include "tracing/tracing.hpp"

namespace bench::impl {

/// Inner aggregate, so expansion has two levels to walk.
struct Limits {
  int max_depth;
  bool allow_dupes;
};

/// Expands to `opts.limits.max_depth`, `opts.limits.allow_dupes`,
/// `opts.timeout_s`.
struct Opts {
  Limits limits;
  double timeout_s;
};

/// Not an aggregate (private members) and not convertible to an
/// `AttributeValue`, but streamable, so it reaches record_value()'s third
/// branch: rendered through an `ostringstream` and recorded as text.
class Version {
 public:
  constexpr Version(const char* tag, int patch) : m_tag(tag), m_patch(patch) {}

  constexpr int patch() const { return m_patch; }

  friend std::ostream& operator<<(std::ostream& os, const Version& v) {
    return os << v.m_tag << '.' << v.m_patch;
  }

 private:
  const char* m_tag;
  int m_patch;
};

/// Untraced baseline. Called directly, never wrapped.
int raw_impl(int n);

/// Wrapped with `TracedFn`, the variadic forwarding wrapper.
int plain_impl([[= tracing::record]] int n);

/// Wrapped with `traced_overloads`, the exact-signature wrapper. The second
/// overload exists only to make this a genuine overload set; benchmarks call
/// the `int` one so the work matches `plain_impl` exactly.
int over_impl([[= tracing::record]] int n);
int over_impl([[= tracing::record]] double d);

/// Wrapped with `traced_for`, one instantiation of a function template.
template <typename T>
T tpl_impl([[= tracing::record]] T v);

/// No parameters: isolates span cost from attribute cost.
int none_impl();

/// One unmarked scalar: the recording branch is discarded at compile time, so
/// this is the "traced but records nothing" cost.
int unmarked_impl(int n);

/// One marked string_view: a string attribute rather than an integer one.
int sv_impl([[= tracing::record]] std::string_view s);

/// One marked nested aggregate: three attributes, and record_value() builds a
/// fresh std::string key per leaf per call.
/// TODO: We could improve the perf for deep aggregates by making the consteval
/// member walk emit one define_static_string per leaf and pass a const char*
/// down, instead of concatenating at runtime.
int agg_impl([[= tracing::record]] Opts opts);

/// One marked streamable non-aggregate: record_value() builds an
/// `ostringstream` per call, renders into it and copies the result out of
/// `os.str()`. Called with both a short and a long `Version` to separate that
/// fixed cost from the cost of the char ops (alloc vs sso).
int stream_impl([[= tracing::record]] Version v);

}  // namespace bench::impl

namespace bench {

inline constexpr tracing::TracedFn<^^impl::plain_impl> plain{};
inline constexpr auto over =
    tracing::traced_overloads<^^impl, std::define_static_string("over_impl")>;
template <typename T>
inline constexpr auto tpl = tracing::traced_for<^^impl::tpl_impl, T>;

inline constexpr tracing::TracedFn<^^impl::none_impl> none{};
inline constexpr tracing::TracedFn<^^impl::unmarked_impl> unmarked{};
inline constexpr tracing::TracedFn<^^impl::sv_impl> sv{};
inline constexpr tracing::TracedFn<^^impl::agg_impl> agg{};
inline constexpr tracing::TracedFn<^^impl::stream_impl> stream{};

}  // namespace bench
