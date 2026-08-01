#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "span_test_fixture.hpp"
#include "test_ops.hpp"

namespace {

using tracing::test::SpanTest;

using Attributes = SpanTest;

TEST_F(Attributes, ScalarsLandInTheRightAlternative) {
  scalars(true, -7, 9u, 2.5, 1.5f, std::string_view{"as_view"},
          std::string{"as_string"}, "as_cstr");

  const auto* span = SpanNamed("scalars");
  ASSERT_NE(span, nullptr);

  EXPECT_TRUE(HasAttr<bool>(*span, "flag", true));
  EXPECT_TRUE(HasAttr<std::int64_t>(*span, "signed_v", -7));
  EXPECT_TRUE(HasAttr<std::uint64_t>(*span, "unsigned_v", 9u));
  EXPECT_TRUE(HasAttr<double>(*span, "double_v", 2.5));
  // float widens to double
  EXPECT_TRUE(HasAttr<double>(*span, "float_v", 1.5));
  // All three string flavours collapse into the same std::string alternative.
  EXPECT_TRUE(HasAttr<std::string>(*span, "view_v", std::string{"as_view"}));
  EXPECT_TRUE(
      HasAttr<std::string>(*span, "string_v", std::string{"as_string"}));
  EXPECT_TRUE(HasAttr<std::string>(*span, "cstr_v", std::string{"as_cstr"}));
}

TEST_F(Attributes, ParameterNamesComeFromTheDeclaration) {
  parse("timeout=30", true, impl::Opts{{8, false}, 1.5, impl::Mode::kSafe});

  const auto* span = SpanNamed("cfg.parse");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::string>(*span, "text", std::string{"timeout=30"}));
  EXPECT_TRUE(HasAttr<bool>(*span, "strict", true));
}

TEST_F(Attributes, NestedAggregatesFlattenIntoDottedPaths) {
  parse("timeout=30", true, impl::Opts{{8, false}, 1.5, impl::Mode::kSafe});

  EXPECT_EQ(AttrKeys("cfg.parse"),
            (std::vector<std::string>{"opts.limits.allow_dupes",
                                      "opts.limits.max_depth", "opts.mode",
                                      "opts.timeout_s", "strict", "text"}));

  const auto* span = SpanNamed("cfg.parse");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::int64_t>(*span, "opts.limits.max_depth", 8));
  EXPECT_TRUE(HasAttr<bool>(*span, "opts.limits.allow_dupes", false));
  EXPECT_TRUE(HasAttr<double>(*span, "opts.timeout_s", 1.5));
}

TEST_F(Attributes, EnumsAreRecordedByEnumeratorName) {
  parse("x", false, impl::Opts{{1, true}, 0.5, impl::Mode::kSafe});

  const auto* span = SpanNamed("cfg.parse");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::string>(*span, "opts.mode", std::string{"kSafe"}));
}

TEST_F(Attributes, AnEnumValueWithNoEnumeratorFallsBackToItsInteger) {
  warm_cache(16, impl::Version{2, 7}, static_cast<impl::Mode>(99));

  const auto* span = SpanNamed("warm_cache");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::int64_t>(*span, "mode", 99));
}

TEST_F(Attributes, AStreamableTypeIsRenderedViaOperatorShiftAndCopiedEagerly) {
  warm_cache(16, impl::Version{2, 7}, impl::Mode::kFast);

  const auto* span = SpanNamed("warm_cache");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::string>(*span, "build", std::string{"2.7"}));
  EXPECT_TRUE(HasAttr<std::int64_t>(*span, "slots", 16));
}

TEST_F(Attributes, AFunctionWithNoMarkedParameterRecordsNothing) {
  EXPECT_DOUBLE_EQ(scale(2.5, 4.0), 10.0);

  const auto* span = SpanNamed("scale");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(span->GetAttributes().empty());
  EXPECT_TRUE(AttrKeys("scale").empty());
}

TEST_F(Attributes, AnUnmarkedParameterIsSuppressedBesideAMarkedOne) {
  EXPECT_EQ(handle_request("/v1/orders", impl::Credentials{"s3cr3t"}), 10);

  EXPECT_EQ(AttrKeys("handle_request"), (std::vector<std::string>{"route"}));

  const auto* span = SpanNamed("handle_request");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::string>(*span, "route", std::string{"/v1/orders"}));
}

TEST_F(Attributes, KeysComeFromTheParameterNamesVisibleToTheCallingTu) {
  EXPECT_EQ(renamed(2, 3), 5);

  EXPECT_EQ(AttrKeys("renamed"), (std::vector<std::string>{"first", "second"}));
}

using TemplateAttributes = SpanTest;

TEST_F(TemplateAttributes,
       TracedForRecordsTheMarkedParametersOfAnInstantiation) {
  render_json<std::string>(std::string{"abc"}, "utf-8");

  EXPECT_EQ(AttrKeys("render_json"), (std::vector<std::string>{"charset"}));

  const auto* span = SpanNamed("render_json");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::string>(*span, "charset", std::string{"utf-8"}));
}

TEST_F(TemplateAttributes, ABareTemplateWrapperRecordsNothingAtAll) {
  render_any(std::string{"abc"}, "utf-8");

  const auto* span = SpanNamed("render_json");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(span->GetAttributes().empty());
}

TEST_F(TemplateAttributes, BothTemplatePathsShareOneSpanName) {
  render_json<std::string>(std::string{"abc"}, "utf-8");
  render_json<std::vector<int>>(std::vector<int>{1, 2}, "ascii");

  EXPECT_EQ(CountNamed("render_json"), 2u);
  EXPECT_EQ(SpanNamed("render_json<std::string>"), nullptr);
}

TEST_F(TemplateAttributes, ANonTypeTemplateParameterRecordsThroughASubstitute) {
  EXPECT_EQ(bump_by_5(10), 15);

  const auto* span = SpanNamed("bump_by");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::int64_t>(*span, "value", 10));
}

using OverloadAttributes = SpanTest;

TEST_F(OverloadAttributes, EachOverloadRecordsItsOwnParameters) {
  EXPECT_EQ(pick(std::string_view{"abcd"}), 4);
  EXPECT_EQ(pick(21), 42);
  EXPECT_EQ(pick(5, true), 10);

  EXPECT_EQ(CountNamed("pick"), 3u);

  // Collect the three attribute key sets; the span order is call order.
  std::vector<std::vector<std::string>> keys;
  for (const auto& span : Spans()) {
    if (NameOf(*span) != "pick") continue;
    std::vector<std::string> k;
    for (const auto& [key, _] : span->GetAttributes()) k.push_back(key);
    std::ranges::sort(k);
    keys.push_back(std::move(k));
  }
  ASSERT_EQ(keys.size(), 3u);
  // The params for the overloaded functions.
  EXPECT_EQ(keys[0], (std::vector<std::string>{"s"}));
  EXPECT_EQ(keys[1], (std::vector<std::string>{"n"}));
  EXPECT_EQ(keys[2], (std::vector<std::string>{"n", "twice"}));
}

TEST_F(OverloadAttributes, AnOverloadSetOfOneStillRecords) {
  EXPECT_EQ(only_one(41), 42);

  const auto* span = SpanNamed("only_one");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(HasAttr<std::int64_t>(*span, "n", 41));
}

// The exact-signature path has its own empty-pack guard, mirroring TracedFn's.
TEST_F(OverloadAttributes,
       ANullaryOverloadWrapperRecordsNothingAndDoesNotBreak) {
  EXPECT_EQ(no_args(), 7);

  const auto* span = SpanNamed("no_args");
  ASSERT_NE(span, nullptr);
  EXPECT_TRUE(span->GetAttributes().empty());
}

}  // namespace
