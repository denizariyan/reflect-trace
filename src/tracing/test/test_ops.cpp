#include "test_ops.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace impl {

void scalars_impl(bool flag, int signed_v, unsigned unsigned_v, double double_v,
                  float float_v, std::string_view view_v, std::string string_v,
                  const char* cstr_v) {
  (void)flag;
  (void)signed_v;
  (void)unsigned_v;
  (void)double_v;
  (void)float_v;
  (void)view_v;
  (void)string_v;
  (void)cstr_v;
}

int parse_impl(std::string_view text, bool strict, Opts opts) {
  return int(text.size()) * (strict ? 2 : 1) + opts.limits.max_depth;
}

void warm_cache_impl(int slots, Version build, Mode mode) {
  (void)slots;
  (void)build;
  (void)mode;
}

double scale_impl(double x, double factor) { return x * factor; }

int handle_request_impl(std::string_view route, const Credentials& creds) {
  (void)creds;
  return int(route.size());
}

void reload_impl(std::string_view path) {
  throw std::runtime_error("cannot read " + std::string(path));
}

void throw_int_impl() { throw 42; }

// nesting
//
// These call the *wrappers*, not each other's `_impl` so that the nested
// functions are also traced. That resolves because test_ops.hpp declares the
// wrappers before this file's first definition, which is the whole reason the
// wrappers live in a header.

int level3_impl() { return 3; }
int level2_impl() { return level3() + 1; }
int level1_impl() { return level2() + 1; }

void child_a_impl() {}
void child_b_impl() {}
void two_children_impl() {
  child_a();
  child_b();
}

int& ref_slot_impl() {
  static int slot = 0;
  return slot;
}

Counted make_counted_impl(int v) { return Counted{v}; }

int consume_impl(std::unique_ptr<int> p) { return p ? *p : -1; }

void bump_impl(int& n) { ++n; }

void nothing_impl() {}

int pick_impl(std::string_view s) { return int(s.size()); }
int pick_impl(int n) { return n * 2; }
int pick_impl(int n, bool twice) { return twice ? n * 2 : n; }

int only_one_impl(int n) { return n + 1; }

int no_args_impl() { return 7; }

int& ref_slot_ovl_impl() {
  static int slot = 0;
  return slot;
}

int consume_ovl_impl(std::unique_ptr<int> p) { return p ? *p : -1; }

// Defaults live on the declaration in test_ops.hpp, not here.
int defaulted_impl(int a, int b, int c) { return a * 100 + b * 10 + c; }
int defaulted_impl(std::string_view s) { return int(s.size()); }

int count_moves_impl(Counted c) { return c.value; }
int count_moves_impl(int n) { return n; }
int count_moves_fn_impl(Counted c) { return c.value; }

int by_ref_ovl_impl(const Counted& c) { return c.value; }
int by_ref_ovl_impl(int n) { return n; }

// Parameter names deliberately disagree with the declaration in test_ops.hpp.
int renamed_impl(int alpha, int beta) { return alpha + beta; }

// Must come after that definition: what a reflection query answers depends on
// how much of the TU the compiler has seen when it is asked.
std::pair<const char*, const char*> RenamedNamesFromDefinitionTu() {
  constexpr auto names = tracing::detail::parameter_names<^^renamed_impl>;
  return {names[0], names[1]};
}

}  // namespace impl
