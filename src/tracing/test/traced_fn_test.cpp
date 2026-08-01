/// @file
/// The wrapper's own semantics: span naming, what it does to a return value,
/// what it does to an argument on the way through, what an escaping exception
/// leaves behind, and how spans end up parented.

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "opentelemetry/trace/span_metadata.h"
#include "span_test_fixture.hpp"
#include "test_ops.hpp"
#include "tracing/tracer.hpp"

namespace {

using opentelemetry::trace::StatusCode;
using tracing::test::SpanTest;

using TracedFn = SpanTest;

TEST_F(TracedFn, UsesTheSpanNameConfigOfDerived) {
  nothing();
  scale(1.0, 1.0);
  parse("x", false, impl::Opts{{1, false}, 0.5, impl::Mode::kFast});

  EXPECT_NE(SpanNamed("nothing"), nullptr);    // `_impl` stripped
  EXPECT_NE(SpanNamed("scale"), nullptr);      // `_impl` stripped
  EXPECT_NE(SpanNamed("cfg.parse"), nullptr);  // explicit .name wins

  EXPECT_EQ(SpanNamed("nothing_impl"), nullptr);
  EXPECT_EQ(SpanNamed("parse"), nullptr);
}

TEST_F(TracedFn, ReportsTheLibrarysInstrumentationScope) {
  nothing();

  const auto* span = SpanNamed("nothing");
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->GetInstrumentationScope().GetName(),
            tracing::kInstrumentationScope);
  EXPECT_EQ(span->GetInstrumentationScope().GetVersion(),
            tracing::kInstrumentationVersion);
}

TEST_F(TracedFn, ReturnsTheWrappedFunctionsValueUnchanged) {
  EXPECT_DOUBLE_EQ(scale(2.5, 4.0), 10.0);
  EXPECT_EQ(
      parse("timeout=30", true, impl::Opts{{8, false}, 1.5, impl::Mode::kSafe}),
      28);
}

TEST_F(TracedFn, HandlesAVoidReturn) {
  static_assert(std::is_void_v<decltype(nothing())>);
  nothing();
  EXPECT_NE(SpanNamed("nothing"), nullptr);
}

TEST_F(TracedFn, PreservesAnLvalueReferenceReturn) {
  static_assert(std::is_same_v<decltype(ref_slot()), int&>);

  ref_slot() = 42;
  EXPECT_EQ(impl::ref_slot_impl(), 42);
}

TEST_F(TracedFn, AddsNeitherACopyNorAMoveOnTheReturnPath) {
  impl::Counted::Reset();

  const impl::Counted c = make_counted(7);

  EXPECT_EQ(c.value, 7);
  EXPECT_EQ(impl::Counted::copies, 0);
  EXPECT_EQ(impl::Counted::moves, 0);
}

TEST_F(TracedFn, ForwardsAnRvalueArgumentAsAnRvalue) {
  auto p = std::make_unique<int>(5);

  EXPECT_EQ(consume(std::move(p)), 5);
  EXPECT_EQ(p, nullptr);
}

TEST_F(TracedFn, PassesAnLvalueReferenceThroughSoTheCalleeCanMutateIt) {
  int n = 1;

  bump(n);

  EXPECT_EQ(n, 2);
}

TEST_F(TracedFn, MarksTheSpanErroredAndRethrowsAStdException) {
  try {
    reload("/etc/missing.conf");
    FAIL() << "reload did not throw";
  } catch (const std::runtime_error& e) {
    EXPECT_STREQ(e.what(), "cannot read /etc/missing.conf");
  }

  const auto* span = SpanNamed("cfg.reload");
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->GetStatus(), StatusCode::kError);
  EXPECT_EQ(DescriptionOf(*span), "cannot read /etc/missing.conf");
}

TEST_F(TracedFn, MarksTheSpanErroredAndRethrowsANonStdException) {
  EXPECT_THROW(throw_int(), int);

  const auto* span = SpanNamed("throw_int");
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->GetStatus(), StatusCode::kError);
  EXPECT_EQ(DescriptionOf(*span), "unknown exception");
}

TEST_F(TracedFn, StillExportsTheSpanWhenTheFunctionThrows) {
  EXPECT_THROW(reload("/x"), std::runtime_error);

  EXPECT_EQ(CountNamed("cfg.reload"), 1u);
}

TEST_F(TracedFn, LeavesASucceedingSpanUnset) {
  nothing();

  const auto* span = SpanNamed("nothing");
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->GetStatus(), StatusCode::kUnset);
}

TEST_F(TracedFn, ParentsEachCallToItsCallerThreeDeep) {
  EXPECT_EQ(level1(), 5);

  const auto* l1 = SpanNamed("level1");
  const auto* l2 = SpanNamed("level2");
  const auto* l3 = SpanNamed("level3");
  ASSERT_NE(l1, nullptr);
  ASSERT_NE(l2, nullptr);
  ASSERT_NE(l3, nullptr);

  EXPECT_EQ(l3->GetParentSpanId(), l2->GetSpanId());
  EXPECT_EQ(l2->GetParentSpanId(), l1->GetSpanId());

  EXPECT_EQ(l2->GetTraceId(), l1->GetTraceId());
  EXPECT_EQ(l3->GetTraceId(), l1->GetTraceId());
}

// Called from a test body with no enclosing span. Without this, a bug that
// parented everything to a single span would still satisfy the assertions
// above.
TEST_F(TracedFn, StartsARootSpanWhenThereIsNoCurrentSpan) {
  EXPECT_EQ(level1(), 5);

  const auto* l1 = SpanNamed("level1");
  ASSERT_NE(l1, nullptr);
  EXPECT_TRUE(IsRoot(*l1));
}

TEST_F(TracedFn, MakesConsecutiveCallsSiblingsRatherThanNesting) {
  two_children();

  const auto* parent = SpanNamed("two_children");
  const auto* a = SpanNamed("child_a");
  const auto* b = SpanNamed("child_b");
  ASSERT_NE(parent, nullptr);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);

  EXPECT_EQ(a->GetParentSpanId(), parent->GetSpanId());
  EXPECT_EQ(b->GetParentSpanId(), parent->GetSpanId());

  EXPECT_NE(b->GetParentSpanId(), a->GetSpanId());
}

TEST_F(TracedFn, StartsSeparateTracesForUnrelatedTopLevelCalls) {
  nothing();
  scale(1.0, 1.0);

  const auto* first = SpanNamed("nothing");
  const auto* second = SpanNamed("scale");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  EXPECT_TRUE(IsRoot(*first));
  EXPECT_TRUE(IsRoot(*second));

  EXPECT_NE(first->GetTraceId(), second->GetTraceId());
}

TEST_F(TracedFn, RunsAgainstTheNoopTracerWhenNoProviderIsInstalled) {
  UninstallProvider();

  EXPECT_DOUBLE_EQ(scale(2.5, 4.0), 10.0);
  EXPECT_EQ(
      parse("timeout=30", true, impl::Opts{{8, false}, 1.5, impl::Mode::kSafe}),
      28);
  EXPECT_THROW(reload("/x"), std::runtime_error);

  EXPECT_TRUE(Spans().empty());
}

TEST_F(TracedFn, WrapsAFunctionTemplateBareAndStillNamesTheSpan) {
  EXPECT_EQ(render_any(std::string{"abcd"}, "utf-8"), "utf-8:4");

  EXPECT_NE(SpanNamed("render_json"), nullptr);
  EXPECT_EQ(SpanNamed("render_json_impl"), nullptr);
}

TEST_F(TracedFn, TracedForReturnsTheInstantiationsValue) {
  EXPECT_EQ(render_json<std::string>(std::string{"ab"}, "ascii"), "ascii:2");
  EXPECT_EQ(bump_by_5(10), 15);
}

template <typename T>
std::string RenderVia(const T& v) {
  return render_json<T>(v, "utf-8");
}

TEST_F(TracedFn, TracedForInstantiatesOnDemandFromAGenericCaller) {
  EXPECT_EQ(RenderVia(std::string{"abc"}), "utf-8:3");
  EXPECT_EQ(RenderVia(std::vector<int>{1, 2, 3, 4}), "utf-8:4");

  // Same span name from both: the template argument is not in the name.
  EXPECT_EQ(CountNamed("render_json"), 2u);
}

TEST_F(TracedFn, DispatchesToTheRightOverload) {
  EXPECT_EQ(pick(std::string_view{"abcd"}), 4);  // string_view overload
  EXPECT_EQ(pick(21), 42);                       // int overload
  EXPECT_EQ(pick(5, false), 5);                  // two-parameter overload

  EXPECT_EQ(CountNamed("pick"), 3u);
}

TEST_F(TracedFn, OverloadsShareTheSpanNameOfTheirIdentifier) {
  pick(1);
  EXPECT_NE(SpanNamed("pick"), nullptr);

  EXPECT_EQ(SpanNamed("pick_impl"), nullptr);
}

TEST_F(TracedFn, ExactSignatureWrapperPreservesAReferenceReturn) {
  static_assert(std::is_same_v<decltype(ref_slot_ovl()), int&>);

  ref_slot_ovl() = 77;
  EXPECT_EQ(ref_slot_ovl(), 77);
}

TEST_F(TracedFn, ExactSignatureWrapperMovesAMoveOnlyArgumentThrough) {
  EXPECT_EQ(consume_ovl(std::make_unique<int>(5)), 5);

  auto p = std::make_unique<int>(9);
  EXPECT_EQ(consume_ovl(std::move(p)), 9);
  EXPECT_EQ(p, nullptr);
}

// The exact-signature wrapper's real cost:
//
//   - from a prvalue, guaranteed copy elision constructs TracedCall's by-value
//     parameter in place, so both wrappers cost exactly one move;
//   - from an xvalue there is nothing to elide, so TracedCall pays a second.
TEST_F(TracedFn, ExactSignaturesCostAnExtraMoveOnlyForAnXvalue) {
  impl::Counted::Reset();
  count_moves_fn(impl::Counted{1});
  EXPECT_EQ(impl::Counted::moves, 1);
  EXPECT_EQ(impl::Counted::copies, 0);

  impl::Counted::Reset();
  count_moves(impl::Counted{1});
  EXPECT_EQ(impl::Counted::moves, 1) << "a prvalue must elide into TracedCall";
  EXPECT_EQ(impl::Counted::copies, 0);

  impl::Counted a{1};
  impl::Counted::Reset();
  count_moves_fn(std::move(a));
  EXPECT_EQ(impl::Counted::moves, 1);

  impl::Counted b{1};
  impl::Counted::Reset();
  count_moves(std::move(b));
  EXPECT_EQ(impl::Counted::moves, 2)
      << "the extra move lives here, and only here";
  EXPECT_EQ(impl::Counted::copies, 0);
}

TEST_F(TracedFn, ExactSignaturesCostNothingForAReferenceParameter) {
  const impl::Counted c{5};

  impl::Counted::Reset();
  EXPECT_EQ(by_ref_ovl(c), 5);
  EXPECT_EQ(impl::Counted::copies, 0) << "a const& parameter must not decay";
  EXPECT_EQ(impl::Counted::moves, 0);

  EXPECT_EQ(by_ref_ovl(7), 7);
}

TEST_F(TracedFn, ExactSignatureWrapperCarriesDefaultArguments) {
  EXPECT_EQ(defaulted(1), 123);        // b and c defaulted
  EXPECT_EQ(defaulted(1, 9), 193);     // c defaulted
  EXPECT_EQ(defaulted(1, 9, 8), 198);  // none defaulted

  // The other overload still resolves, so arity flexibility did not swallow it.
  EXPECT_EQ(defaulted(std::string_view{"abcd"}), 4);
}

// Only the arguments actually passed are recorded, a defaulted parameter has
// no argument at the callsite, so the prefix wrapper never sees it.
TEST_F(TracedFn, ADefaultedParameterIsNotRecorded) {
  defaulted(1);

  const auto* span = SpanNamed("defaulted");
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(AttrKeys("defaulted"), (std::vector<std::string>{"a"}));
}

TEST_F(TracedFn, ExactSignatureWrapperHandlesAZeroParameterFunction) {
  EXPECT_EQ(no_args(), 7);
  EXPECT_NE(SpanNamed("no_args"), nullptr);
}

TEST_F(TracedFn, OverloadSpansParentNormally) {
  two_children();
  EXPECT_NE(SpanNamed("two_children"), nullptr);

  only_one(1);
  const auto* span = SpanNamed("only_one");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(IsRoot(*span));
}

}  // namespace
