# C++26 reflection to OpenTelemetry spans

Derive span names and attributes from your declarations, at compile time.

Mark functions and templates at compile-time, get a wrapper that opens a real
OpenTelemetry span, records the parameters by their source names, and expands
nested structs into dotted attribute keys. No macros, no manual `SetAttribute`
or span plumbing, no runtime reflection.

## The idea

Instrumenting a function by hand means restating everything the compiler already
knows:

```cpp
int parse(std::string_view text, bool strict, Opts opts) {
  auto span = tracer->StartSpan("cfg.parse");
  span->SetAttribute("text", text);
  span->SetAttribute("strict", strict);
  span->SetAttribute("opts.limits.max_depth", opts.limits.max_depth);
  span->SetAttribute("opts.limits.allow_dupes", opts.limits.allow_dupes);
  span->SetAttribute("opts.timeout_s", opts.timeout_s);
  span->SetAttribute("opts.mode", int(opts.mode));
  // ... plus handling exceptions, status, etc.
}
```

Every one of those strings above is a compile-time constant that has been retyped by
hand, and will silently rot when a parameter is renamed. The declaration already
contains all of it:

```cpp
[[= tracing::Traced{.name = std::define_static_string("cfg.parse") /* optional override */}]]
int parse_impl(std::string_view text,
               [[= tracing::record]] bool strict /* opt-in parameter recording */,
               [[= tracing::record]] Opts opts);
```

```cpp
inline constexpr tracing::TracedFn<^^impl::parse_impl> parse{};
```

Calling `parse(...)` now produces exactly the span above, same keys, same
types; read out of the declaration by `std::meta` during compile-time via constant evaluation.

## What gets derived

Recorded for a call to `parse("timeout=30", true, {{8, false}, 1.5, Mode::kSafe})`:

| attribute | type in span | where it came from |
|---|---|---|
| `text` | string | parameter name |
| `strict` | bool | parameter name |
| `opts.limits.max_depth` | int64 | recursion into a nested aggregate |
| `opts.limits.allow_dupes` | bool | recursion into a nested aggregate |
| `opts.timeout_s` | float64 | recursion into a nested aggregate |
| `opts.mode` | string `kSafe` | enumerator identifier, not the integer |

Parameter recording is **opt-in, per parameter**, so by default you get a
span and nothing else. An unmarked parameter is never exported and never even
needs a way to be turned into an attribute, so a traced function can take
arguments the library has no idea how to record.

## Try it

>TODO: update with demo code

Everything builds inside the devcontainer.

Open the folder in VS Code -> *Open in Container*, then:

```sh
bazel build //...
bazel test //...
```

## How it works

Mainly a combination of two C++26 features:

- **P2996 reflection:** `^^T` produces a `std::meta::info`; `[:m:]` splices one
  back into an expression. This allows us to read the parameter names and types of a function,
  and to iterate over the members of an aggregate.
- **P3394 annotations:** `[[= Traced{...}]]` attaches an ordinary constant to a
  declaration, read back with `annotations_of`. This allows the user to specify a custom span name,
  or to mark a parameter for recording as an attribute.

`TracedFn<F>` reads the annotation with `config_of<F>()`, derives parameter names
with `parameters_of`, opens a span, and splices a call to `F`. Everything the
span needs is a constant by the time the program runs.

## What C++26 reflection cannot do

There is **no function generation**, no statement injection, or token injection.

The wrapper therefore cannot *be* the function it wraps, it has to be a
separate entity with a separate name.

## Status

This is mostly an exploration and a proof of concept. I do plan to use this in some personal
projects but some of the C++26 features this library relies on are not yet widely available
or stable.
It builds on gcc 16.1 with `-std=c++26 -freflection`, which is the only toolchain that implements
enough of the papers as of mid-2026.