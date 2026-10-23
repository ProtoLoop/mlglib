#include "InplaceString.h"

#include <format>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>

// NOLINTBEGIN(readability-magic-numbers)

namespace
{
template<size_t N>
void
ExpectInplaceString(const InplaceString<N>& value, const std::string_view expected)
{
    ASSERT_EQ(value.size(), expected.size());
    EXPECT_EQ(value.empty(), expected.empty());
    EXPECT_EQ(std::string_view(value), expected);
    EXPECT_EQ(std::string_view(value.data(), value.size()), expected);
    EXPECT_EQ(value.data(), value.c_str());
    EXPECT_EQ(std::string_view(value.c_str(), value.size() + 1).back(), '\0');
    EXPECT_EQ(value.GetHashCode(), InplaceString<N>::Hash(expected));
    EXPECT_EQ(std::hash<InplaceString<N>>{}(value), value.GetHashCode());
}
} // namespace

TEST(InplaceString, DefaultConstructionIsEmpty)
{
    const InplaceString<8> value;
    ExpectInplaceString(value, "");
}

TEST(InplaceString, DefaultCapacityHolds255Characters)
{
    EXPECT_EQ(InplaceString<>::kMaxLength, 255U);
    const std::string expected(255, 'a');
    const InplaceString<> value(expected);
    ExpectInplaceString(value, expected);
}

TEST(InplaceString, ConstructsAtExactCapacity)
{
    const InplaceString<5> value("hello");
    ExpectInplaceString(value, "hello");
}

TEST(InplaceString, AssignsAtExactCapacity)
{
    InplaceString<5> value("a");
    auto& result = (value = std::string_view("hello"));
    EXPECT_EQ(&result, &value);
    ExpectInplaceString(value, "hello");
}

TEST(InplaceString, AssignsShorterAndEmptyStrings)
{
    InplaceString<5> value("hello");
    value = "hi";
    ExpectInplaceString(value, "hi");
    EXPECT_EQ(value, InplaceString<5>("hi"));

    value = "";
    ExpectInplaceString(value, "");
    EXPECT_EQ(value, InplaceString<5>());
}

TEST(InplaceString, CopiesNonTerminatedView)
{
    char source[] = { 'a', 'b', 'c' };
    const InplaceString<3> constructed(std::string_view(&source[0], sizeof(source)));
    InplaceString<3> assigned;
    assigned = std::string_view(&source[0], sizeof(source));
    source[0] = 'z';

    ExpectInplaceString(constructed, "abc");
    ExpectInplaceString(assigned, "abc");
}

TEST(InplaceString, PreservesEmbeddedNullCharacters)
{
    constexpr char source[] = { 'a', '\0', 'b' };
    const std::string_view expected(&source[0], sizeof(source));
    const InplaceString<3> constructed(expected);
    InplaceString<3> assigned;
    assigned = expected;

    ExpectInplaceString(constructed, expected);
    ExpectInplaceString(assigned, expected);
    EXPECT_EQ(constructed, assigned);
    EXPECT_NE(constructed, InplaceString<3>("a"));
}

TEST(InplaceString, CopyConstructionOwnsItsContents)
{
    InplaceString<5> source("hello");
    const InplaceString<5> copy(source);
    source = "bye";

    ExpectInplaceString(copy, "hello");
    ExpectInplaceString(source, "bye");
}

TEST(InplaceString, CopyAssignmentReplacesContentsAndHash)
{
    InplaceString<5> source("hi");
    InplaceString<5> destination("hello");
    destination = source;
    source = "bye";

    ExpectInplaceString(destination, "hi");
    ExpectInplaceString(source, "bye");
}

TEST(InplaceString, MoveConstructionPreservesContentsAndHash)
{
    InplaceString<5> source("hello");
    // Exercise the rvalue operation even though InplaceString is trivially copyable.
    // NOLINTNEXTLINE(performance-move-const-arg)
    const InplaceString<5> destination(std::move(source));
    ExpectInplaceString(destination, "hello");
}

TEST(InplaceString, MoveAssignmentReplacesContentsAndHash)
{
    InplaceString<5> source("hi");
    InplaceString<5> destination("hello");
    // NOLINTNEXTLINE(performance-move-const-arg)
    destination = std::move(source);
    ExpectInplaceString(destination, "hi");
}

TEST(InplaceString, ConstructsFromSmallerCapacity)
{
    InplaceString<3> source("abc");
    const InplaceString<8> destination(source);
    source = "z";

    ExpectInplaceString(destination, "abc");
    ExpectInplaceString(source, "z");
}

TEST(InplaceString, ComparesContentsLexicographically)
{
    const InplaceString<5> first("alpha");
    const InplaceString<5> equal("alpha");
    const InplaceString<5> later("beta");
    const InplaceString<5> prefix("alp");

    EXPECT_EQ(first, equal);
    EXPECT_FALSE(first != equal);
    EXPECT_NE(first, later);
    EXPECT_LT(first, later);
    EXPECT_GT(later, first);
    EXPECT_LT(prefix, first);
    EXPECT_LE(first, equal);
    EXPECT_GE(first, equal);
    EXPECT_EQ(first.GetHashCode(), equal.GetHashCode());
}

TEST(InplaceString, FormatsBelowCapacity)
{
    const auto value = InplaceString<16>::Format("{}:{}", "item", 7);
    ExpectInplaceString(value, "item:7");
}

TEST(InplaceString, FormatsEmptyString)
{
    const auto value = InplaceString<5>::Format("{}", "");
    ExpectInplaceString(value, "");
}

TEST(InplaceString, FormatsAtExactCapacityWithoutEllipsis)
{
    const auto value = InplaceString<5>::Format("{}", "hello");
    ExpectInplaceString(value, "hello");
}

TEST(InplaceString, FormattingOverflowAppendsEllipsis)
{
    const auto value = InplaceString<5>::Format("{}", "abcdef");
    ExpectInplaceString(value, "ab...");
    EXPECT_EQ(value, InplaceString<5>("ab..."));
}

TEST(InplaceString, ZeroCapacityStoresEmptyString)
{
    InplaceString<0> value;
    ExpectInplaceString(value, "");
    value = "";
    ExpectInplaceString(value, "");
    ExpectInplaceString(InplaceString<0>(""), "");
    ExpectInplaceString(InplaceString<0>::Format("{}", "abc"), "");
}

TEST(InplaceString, FormattingWithSmallCapacities)
{
    ExpectInplaceString(InplaceString<1>::Format("{}", "a"), "a");
    ExpectInplaceString(InplaceString<2>::Format("{}", "ab"), "ab");
    ExpectInplaceString(InplaceString<3>::Format("{}", "abc"), "abc");

    ExpectInplaceString(InplaceString<1>::Format("{}", "abcd"), "a");
    ExpectInplaceString(InplaceString<2>::Format("{}", "abcd"), "ab");
    ExpectInplaceString(InplaceString<3>::Format("{}", "abcd"), "...");
}

TEST(InplaceString, SupportsFmtFormatting)
{
    const InplaceString<5> value("hello");
    EXPECT_EQ(fmt::format("[{}]", value), "[hello]");
    EXPECT_EQ(fmt::format("{:>7}", value), "  hello");
}

TEST(InplaceString, SupportsStdFormatting)
{
    const InplaceString<5> value("hello");
    EXPECT_EQ(std::format("[{}]", value), "[hello]");
    EXPECT_EQ(std::format("{:>7}", value), "  hello");
}

// NOLINTEND(readability-magic-numbers)
