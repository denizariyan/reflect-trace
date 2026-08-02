#include "bench_ops.hpp"

namespace bench::impl {

int raw_impl(int n) { return n + 1; }

int plain_impl(int n) { return n + 1; }

int over_impl(int n) { return n + 1; }
int over_impl(double d) { return static_cast<int>(d) + 1; }

template <typename T>
T tpl_impl(T v) {
  return v + 1;
}
// Explicit instantiation, so the `int` specialisation is a real out-of-line
// symbol like the other three.
template int tpl_impl<int>(int);

int none_impl() { return 1; }

int unmarked_impl(int n) { return n + 1; }

int sv_impl(std::string_view s) { return static_cast<int>(s.size()); }

int agg_impl(Opts opts) { return opts.limits.max_depth; }

int stream_impl(Version v) { return v.patch(); }

}  // namespace bench::impl
