# Benchmarks

## What we measure

The question this benchmark exists to answer:

> Given that you were going to open a span anyway, how much does using our wrappers cost
> in runtime compared to writing the instrumentation by hand? And does it differ between
> a plain function, an overload set, and a function template?

It is **not** a benchmark of the OpenTelemetry SDK. The SDK's per-span cost shows
up in every number here and dominates most of them, but that is not the subject.

| benchmark | what it isolates | what is measured |
|---|---|---|
| `Shape_Raw` | untraced call | no instrumentation |
| `Shape_Manual` | hand-written OTel span, same name + same attribute | baseline tracing cost |
| `Shape_TracedFn` | plain function through `TracedFn` | **this library** |
| `Shape_TracedCall` | overload set through `traced_overloads` | **this library** |
| `Shape_TracedFor` | template instantiation through `traced_for` | **this library** |
| `Payload_*` | what `record_value` costs per parameter shape | **this library** |
| `Part_*` | decomposition of one span | OTel span cost |

## Running them

```bash
bazel run --config=benchmark //src/tracing/bench:wrapper_bench
```

## Results

aarch64, gcc 16.1.0, `-c opt`. Median of 20 repetitions ± stddev.

### The library's own overhead

#### With real OTel SDK provider installed

Against `SdkProvider`, i.e. a real OTel SDK tracer where attributes are recorded:

| | ns | vs hand-written |
|---|---|---|
| hand-written span (`Shape_Manual`) | 298 ± 2.7 | — |
| `TracedFn` (plain function) | 300 ± 1.9 | +2 ns |
| `traced_overloads` (overload set) | 302 ± 2.0 | +4 ns |
| `traced_for` (template) | 302 ± 3.1 | +4 ns |

All three shapes land within ~1% of the manual baseline and the differences
between them are smaller than the run-to-run variation. **No measurable overhead.**

#### With no-op provider installed

Against the no-op tracer, i.e. minimal overhead from the OTel SDK itself:

| | ns | vs hand-written |
|---|---|---|
| untraced call (`Shape_Raw`) | 0.69 ± 0.02 | — |
| hand-written span (`Shape_Manual`) | 50.8 ± 1.25 | — |
| `TracedFn` | 51.5 ± 0.38 | +0.7 ns |
| `traced_overloads` | 51.6 ± 0.47 | +0.9 ns |
| `traced_for` | 51.4 ± 0.31 | +0.7 ns |

The differences between the shapes are smaller than the run-to-run variation.
**No measurable overhead.**

### Function type makes no difference

Plain function, overload set and template instantiation are within **0.2 ns** of
each other at the floor and within **2 ns** at the ceiling, smaller than the
run-to-run variation.

The compiler does **not** inline the tracing steps but what it emits is the
same shape for all three. In `wrapper_bench` (`-c opt`, aarch64) every
`run_traced` instantiation for the three shape subjects is **192 instructions**,
out of line:

```
run_traced<^^bench::impl::plain_impl, ...>    192 instructions   (TracedFn)
run_traced<^^bench::impl::over_impl,  ...>    192 instructions   (TracedCall)
run_traced<^^bench::impl::tpl_impl<int>, ...> 192 instructions   (traced_for)
```

The callsites are identical across the three:

```asm
call_raw:                                  ; 1 instruction, tail call
    b     impl::raw_impl

call_plain / call_over / call_tpl:         ; 10 instructions, identical
    stp   x29, x30, [sp, #-48]!
    mov   x29, sp
    add   x1, sp, #0x20
    str   w0, [sp, #28]                    ; spill the int...
    add   x0, sp, #0x1c                    ; ...so a lambda can capture it
    stp   x0, x0, [sp, #32]
    add   x0, sp, #0x28
    bl    tracing::detail::run_traced<...>
    ldp   x29, x30, [sp], #48
    ret
```

### What parameters cost

We allow the user to mark parameters to be recorded as attributes as part of the spans.
Recording a parameter has a cost, and the cost depends on the parameter's type and how it is recorded. The following table shows the measured cost of recording one parameter of various shapes.

| | ns | delta | `record_value` branch |
|---|---|---|---|
| no parameters | 249 ± 1.4 | - | - |
| one **unmarked** scalar | 249 ± 2.4 | within noise | discarded |
| one marked scalar (recorded as `int64`) | 302 ± 3.0 | +53 | `attribute_convertible` |
| one marked `string_view` (recorded as `string`) | 319 ± 4.1 | +70 | `attribute_convertible` |
| one marked streamable, 6-char render | 400 ± 5.9 | +151 | `streamable` |
| one marked streamable, 37-char render (no SSO) | 430 ± 5.9 | +181 | `streamable` |
| one marked nested aggregate, 3 leaves | 466 ± 5.1 | +218 | `expandable_aggregate` |

1. **An unmarked parameter is free** or measurably indistinguishable
   from having no parameter at all.
2. **The `streamable` branch costs ~81 ns on top of recording the same string
   directly.** Rows 4 and 5 are both a 6-character string attribute — the
   `string_view` row passes `"orders"`, the streamable row `"12.345"`.
   So the difference between them is `std::ostringstream` construction and
   teardown and nothing else. The streamable fallback is the most expensive
   per-attribute path in the library by a wide margin. Prefer a `to_attribute`
   overload for any type recorded on a hot path.
3. **Characters are cheap next to the stream itself.** Rows 5 and 6 pass the
   *same type* with different values. Same instantiation, same three insertions
   and 31 extra characters cost ~30 ns, crossing libstdc++'s 15-char SSO
   buffer in both `os.str()` and the SDK's `OwnedAttributeValue` copy. So roughly
   four fifths of a short streamable attribute is fixed overhead that a longer
   payload does not meaningfully increase.
4. **An aggregate leaf costs ~73 ns against ~53 ns for a flat scalar.** That
   ~20 ns/leaf gap is `record_value` building a fresh `std::string` per leaf per
   call via `key + "." + identifier_of(m)`. See TODO on `agg_impl` in
   `bench_ops.hpp` for a possible improvement.
