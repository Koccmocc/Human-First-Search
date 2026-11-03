#include <robots.h>
#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

// These fixtures contain already normalized ASCII URLs; the matcher is not a URL parser.
auto allows(std::string_view rules, std::string_view path,
            std::string_view agent = "HumanFirstSearch") -> bool
{
    googlebot::RobotsMatcher matcher;
    return matcher.OneAgentAllowedByRobots(
        absl::string_view{rules.data(), rules.size()}, std::string{agent},
        "https://example.org" + std::string{path});
}

} // namespace

TEST(RobotsParser, EmptyDocumentAndEmptyDisallowPermitAccess)
{
    EXPECT_TRUE(allows("", "/page"));
    EXPECT_TRUE(allows("User-agent: *\nDisallow:\n", "/page"));
}

TEST(RobotsParser, WildcardGroupBlocksMatchingPaths)
{
    constexpr auto rules = "User-agent: *\nDisallow: /private\n";
    EXPECT_FALSE(allows(rules, "/private/page"));
    EXPECT_TRUE(allows(rules, "/public/page"));
    EXPECT_TRUE(allows(rules, "/Private/page"));
}

TEST(RobotsParser, LongerRulesWinAndAllowWinsAnEqualMatch)
{
    constexpr auto rules =
        "User-agent: *\nDisallow: /private\nAllow: /private/public\n"
        "Disallow: /same\nAllow: /same\n";
    EXPECT_TRUE(allows(rules, "/private/public/page"));
    EXPECT_FALSE(allows(rules, "/private/secret"));
    EXPECT_TRUE(allows(rules, "/same"));
}

TEST(RobotsParser, SpecificUserAgentOverridesWildcardGroup)
{
    constexpr auto rules = "User-agent: *\nDisallow: /\n\n"
        "User-agent: HumanFirstSearch\nDisallow: /private\n";
    EXPECT_TRUE(allows(rules, "/public"));
    EXPECT_FALSE(allows(rules, "/private"));
    EXPECT_FALSE(allows(rules, "/public", "OtherBot"));
}

TEST(RobotsParser, HandlesPatternWildcardsAndEndAnchors)
{
    constexpr auto rules = "User-agent: *\nDisallow: /*.pdf$\n";
    EXPECT_FALSE(allows(rules, "/docs/paper.pdf"));
    EXPECT_TRUE(allows(rules, "/docs/paper.pdf?download=1"));
}

TEST(RobotsParser, HandlesCommentsAndMatchingQueryStrings)
{
    constexpr auto rules = "# a comment\r\nUser-agent: *\r\n"
        "Disallow: /search?q= # ignore the rest\r\nAllow: /search?public=\r\n";
    EXPECT_FALSE(allows(rules, "/search?q=cpp"));
    EXPECT_TRUE(allows(rules, "/search?public=cpp"));
}
