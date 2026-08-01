/// @file
/// Compile-time tests for detail/reflect.hpp and the attributes.hpp concepts.
///
/// Every claim here is a `static_assert`, so a failure is a build failure.
/// Gtest part is only to get the tests by their name in the report like other
/// tests.

#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "test_ops.hpp"
#include "tracing/tracing.hpp"

namespace {

using tracing::detail::attribute_convertible;
using tracing::detail::config_of;
using tracing::detail::enum_name;
using tracing::detail::enum_table;
using tracing::detail::expandable_aggregate;
using tracing::detail::identifier_for;
using tracing::detail::is_parameter_recorded;
using tracing::detail::member_count;
using tracing::detail::members_of;
using tracing::detail::parameter_names;
using tracing::detail::streamable;

/// `config_of` yields a `const char*`; comparing those compares pointers.
consteval bool same(const char* a, const char* b) {
  return std::string_view{a} == std::string_view{b};
}

TEST(ConfigOf, StripsTheImplSuffix) {
  static_assert(same(config_of<^^impl::naming::plain_impl>().name, "plain"));
  SUCCEED();
}

TEST(ConfigOf, LeavesAnUnsuffixedIdentifierAlone) {
  static_assert(same(config_of<^^impl::naming::bare>().name, "bare"));
  SUCCEED();
}

TEST(ConfigOf, DoesNotStripAnIdentifierThatIsOnlyTheSuffix) {
  static_assert(same(config_of<^^impl::naming::_impl>().name, "_impl"));
  SUCCEED();
}

TEST(ConfigOf, MatchesOnSuffixRatherThanSubstring) {
  static_assert(same(config_of<^^impl::naming::implicit>().name, "implicit"));
  static_assert(
      same(config_of<^^impl::naming::_impl_detail>().name, "_impl_detail"));
  SUCCEED();
}

TEST(ConfigOf, ExplicitNameBeatsBothTheIdentifierAndTheSuffixRule) {
  static_assert(
      same(config_of<^^impl::naming::named_impl>().name, "custom.name"));
  static_assert(same(config_of<^^impl::naming::explicit_suffix_impl>().name,
                     "keep_impl"));
  SUCCEED();
}

TEST(ConfigOf, DerivesANameEvenWithoutAnAnnotation) {
  static_assert(
      same(config_of<^^impl::naming::unannotated_impl>().name, "unannotated"));
  SUCCEED();
}

TEST(ParameterNames, ResolveInDeclarationOrder) {
  constexpr auto names = parameter_names<^^impl::parse_impl>;
  static_assert(names.size() == 3);
  static_assert(same(names[0], "text"));
  static_assert(same(names[1], "strict"));
  static_assert(same(names[2], "opts"));
  SUCCEED();
}

TEST(ParameterNames, AreEmptyForANullaryFunction) {
  static_assert(parameter_names<^^impl::nothing_impl>.empty());
  SUCCEED();
}

TEST(ParameterNames, ResolveNormallyInATuThatOnlySawTheDeclaration) {
  constexpr auto names = parameter_names<^^impl::renamed_impl>;
  static_assert(names.size() == 2);
  static_assert(same(names[0], "first"));
  static_assert(same(names[1], "second"));
  SUCCEED();
}

TEST(ParameterNames, FallBackToArgNInTheTuThatSawMultipleSpellings) {
  const auto [first, second] = impl::RenamedNamesFromDefinitionTu();
  EXPECT_STREQ(first, "arg0");
  EXPECT_STREQ(second, "arg1");
}

TEST(IsParameterRecorded, IsTrueForEveryMarkedParameter) {
  constexpr auto marks = is_parameter_recorded<^^impl::parse_impl>;
  static_assert(marks.size() == 3);
  static_assert(marks[0] && marks[1] && marks[2]);
  SUCCEED();
}

TEST(IsParameterRecorded, IsFalseThroughoutWhenNothingIsMarked) {
  constexpr auto marks = is_parameter_recorded<^^impl::scale_impl>;
  static_assert(marks.size() == 2);
  static_assert(!marks[0] && !marks[1]);
  SUCCEED();
}

TEST(IsParameterRecorded,
     DistinguishesAMarkedParameterFromAnUnmarkedNeighbour) {
  constexpr auto marks = is_parameter_recorded<^^impl::handle_request_impl>;
  static_assert(marks.size() == 2);
  static_assert(marks[0]);
  static_assert(!marks[1]);
  SUCCEED();
}

// A mark on a parameter and a lost identifier are independent failures: the
// name degrades to `arg0`, the mark survives intact.
TEST(IsParameterRecorded, SurvivesADeclDefParameterNameMismatch) {
  constexpr auto marks = is_parameter_recorded<^^impl::renamed_impl>;
  static_assert(marks[0] && marks[1]);
  SUCCEED();
}

// record_parameter() indexes both spans with the same I, so a length mismatch
// would be an out-of-bounds read rather than a wrong name.
TEST(IsParameterRecorded, IsIndexedInLockstepWithParameterNames) {
  static_assert(is_parameter_recorded<^^impl::parse_impl>.size() ==
                parameter_names<^^impl::parse_impl>.size());
  static_assert(is_parameter_recorded<^^impl::scalars_impl>.size() ==
                parameter_names<^^impl::scalars_impl>.size());
  SUCCEED();
}

TEST(EnumTable, IsSortedByValueRatherThanDeclarationOrder) {
  constexpr auto table = enum_table<impl::Reordered>;
  static_assert(table.size() == 3);
  static_assert(table[0].value == impl::Reordered::kOne);
  static_assert(table[1].value == impl::Reordered::kTwo);
  static_assert(table[2].value == impl::Reordered::kThree);
  static_assert(same(table[0].name, "kOne"));
  static_assert(same(table[1].name, "kTwo"));
  static_assert(same(table[2].name, "kThree"));
  SUCCEED();
}

TEST(EnumTable, DeduplicatesAliasedEnumerators) {
  constexpr auto table = enum_table<impl::Aliased>;
  static_assert(table.size() == 2);
  static_assert(same(enum_name(impl::Aliased::kUno), "kOne"));
  SUCCEED();
}

TEST(EnumTable, HandlesUnscopedEnums) {
  static_assert(enum_table<impl::Unscoped>.size() == 2);
  static_assert(same(enum_name(impl::kBeta), "kBeta"));
  SUCCEED();
}

TEST(EnumTable, SortsOnTheUnderlyingTypeIncludingNegativeValues) {
  constexpr auto table = enum_table<impl::Signed>;
  static_assert(table.size() == 3);
  static_assert(table[0].value == impl::Signed::kNeg);
  static_assert(table[1].value == impl::Signed::kZero);
  static_assert(table[2].value == impl::Signed::kPos);
  static_assert(same(table[0].name, "kNeg"));
  static_assert(same(table[1].name, "kZero"));
  static_assert(same(table[2].name, "kPos"));
  SUCCEED();
}

TEST(EnumName, ReturnsTheIdentifierForAMatch) {
  static_assert(same(enum_name(impl::Mode::kSafe), "kSafe"));
  SUCCEED();
}

TEST(EnumName, ReturnsNullptrForAValueWithNoEnumerator) {
  static_assert(enum_name(static_cast<impl::Mode>(99)) == nullptr);
  SUCCEED();
}

TEST(MembersOf, ListsDirectNonStaticDataMembersInDeclarationOrder) {
  static_assert(member_count<impl::Opts>() == 3);
  static_assert(member_count<impl::Limits>() == 2);
  static_assert(std::meta::identifier_of(members_of<impl::Opts>[0]) ==
                "limits");
  static_assert(std::meta::identifier_of(members_of<impl::Opts>[1]) ==
                "timeout_s");
  static_assert(std::meta::identifier_of(members_of<impl::Opts>[2]) == "mode");
  SUCCEED();
}

TEST(MembersOf, IsEmptyForATypeWithNoDataMembers) {
  static_assert(member_count<impl::Empty>() == 0);
  SUCCEED();
}

TEST(Concepts, StringIsConvertibleAndNotWalkedAsAnAggregate) {
  static_assert(attribute_convertible<std::string>);
  // The real hazard: libstdc++'s std::string has an *anonymous union* among its
  // direct members, so identifier_of would throw at instantiation if the walk
  // ever reached it. is_aggregate_v is what keeps it out.
  static_assert(!expandable_aggregate<std::string>);
  SUCCEED();
}

TEST(Concepts, VectorIsNotAnExpandableAggregate) {
  static_assert(!expandable_aggregate<std::vector<int>>);
  SUCCEED();
}

TEST(Concepts, AnEmptyAggregateIsNotExpandable) {
  // Otherwise record_value() would take the aggregate branch and record nothing
  // at all, rather than falling through to streamable or to the hard error.
  static_assert(!expandable_aggregate<impl::Empty>);
  SUCCEED();
}

TEST(Concepts, NestedAggregatesAreExpandable) {
  static_assert(expandable_aggregate<impl::Opts>);
  static_assert(expandable_aggregate<impl::Limits>);
  SUCCEED();
}

TEST(Concepts, AStreamableNonAggregateReachesOnlyTheStreamableBranch) {
  static_assert(!attribute_convertible<impl::Version>);
  static_assert(!expandable_aggregate<impl::Version>);
  static_assert(streamable<impl::Version>);
  SUCCEED();
}

TEST(Concepts, AnUnrecordableTypeSatisfiesNoBranch) {
  static_assert(!attribute_convertible<impl::Credentials>);
  static_assert(!expandable_aggregate<impl::Credentials>);
  static_assert(!streamable<impl::Credentials>);
  SUCCEED();
}

/// `render_json_impl` instantiated for two different `T`.
constexpr auto kRenderDouble =
    std::meta::substitute(^^impl::render_json_impl, {
                                                        ^^std::string});
constexpr auto kRenderVector =
    std::meta::substitute(^^impl::render_json_impl, {
                                                        ^^std::vector<int>});

TEST(IdentifierFor, BridgesTheTemplateSpecialisationAsymmetry) {
  static_assert(std::meta::is_function_template(^^impl::render_json_impl));
  static_assert(!std::meta::is_function(^^impl::render_json_impl));
  static_assert(std::meta::is_function(kRenderDouble));
  static_assert(!std::meta::has_identifier(kRenderDouble));

  // Raw, suffix still on: stripping is config_of's job, not this one's.
  static_assert(identifier_for(^^impl::render_json_impl) == "render_json_impl");
  static_assert(identifier_for(kRenderDouble) == "render_json_impl");
  SUCCEED();
}

TEST(ConfigOf, ResolvesTheNameOfASpecialisationThroughItsTemplate) {
  static_assert(same(config_of<kRenderDouble>().name, "render_json"));
  SUCCEED();
}

// This is one case we can't handle same between the template and the
// specialisation.
TEST(ConfigOf, ABareTemplateWrapperIgnoresAnExplicitName) {
  constexpr auto spec =
      std::meta::substitute(^^impl::named_tpl_impl, {
                                                        ^^int});

  // Through the specialisation: the annotation wins.
  static_assert(same(config_of<spec>().name, "custom.tpl"));
  // Through the bare template: silently derived from the identifier.
  static_assert(same(config_of<^^impl::named_tpl_impl>().name, "named_tpl"));
  SUCCEED();
}

TEST(ConfigOf, DoesNotPutTemplateArgumentsInTheSpanName) {
  static_assert(same(config_of<kRenderVector>().name, "render_json"));
  static_assert(
      same(config_of<kRenderDouble>().name, config_of<kRenderVector>().name));
  SUCCEED();
}

TEST(ParameterNames, ResolveThroughASpecialisation) {
  static_assert(parameter_names<kRenderDouble>.size() == 2);
  static_assert(same(parameter_names<kRenderDouble>[0], "body"));
  static_assert(same(parameter_names<kRenderDouble>[1], "charset"));
  SUCCEED();
}

// Names and marks live on the *template's declaration*, not on any one
// instantiation, so they must not vary with `T`.
TEST(ParameterNames, AreIdenticalAcrossTwoInstantiations) {
  static_assert(same(parameter_names<kRenderDouble>[0],
                     parameter_names<kRenderVector>[0]));
  static_assert(same(parameter_names<kRenderDouble>[1],
                     parameter_names<kRenderVector>[1]));
  static_assert(is_parameter_recorded<kRenderDouble>[0] ==
                is_parameter_recorded<kRenderVector>[0]);
  SUCCEED();
}

TEST(IsParameterRecorded, SurvivesSubstitution) {
  static_assert(!is_parameter_recorded<kRenderDouble>[0]);
  static_assert(is_parameter_recorded<kRenderDouble>[1]);
  SUCCEED();
}

/// How many functions in `impl` the library's own scan matches for @p name.
consteval std::size_t matched(std::string_view name) {
  return tracing::detail::matching_functions(^^impl, name).size();
}

TEST(Overloads, AreFoundByScanningTheNamespaceForTheIdentifier) {
  static_assert(matched("pick_impl") == 3);
  SUCCEED();
}

TEST(Overloads, SkipFunctionTemplatesEntirely) {
  static_assert(matched("render_json_impl") == 0);
  SUCCEED();
}

TEST(Overloads, OfDifferentNamesInOneNamespaceDoNotInterfere) {
  static_assert(matched("pick_impl") == 3);
  static_assert(matched("defaulted_impl") == 2);
  static_assert(matched("count_moves_impl") == 2);
  static_assert(matched("only_one_impl") == 1);
  SUCCEED();
}

TEST(Overloads, AnUnmatchedNameMatchesNothing) {
  static_assert(matched("piick_impl") == 0);
  static_assert(matched("") == 0);

  // The suffix is part of the name: the wrapper's own identifier never matches.
  static_assert(matched("pick") == 0);
  SUCCEED();
}

}  // namespace
