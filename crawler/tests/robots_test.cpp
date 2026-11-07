#include "hfs/robots.hpp"
#include "hfs/robots_cache.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

TEST(Robots, AllowsKnownEmptyRulesForValidUrls)
{
    EXPECT_TRUE(hfs::robots_allows(std::string_view{}, "https://example.org/page"));
}

TEST(Robots, UsesCrawlerProductGroupInsteadOfWildcard)
{
    constexpr auto rules = "User-agent: *\nDisallow: /\n\n"
        "User-agent: HumanFirstSearch\nDisallow: /private\nAllow: /private/public\n";
    EXPECT_TRUE(hfs::robots_allows(rules, "https://example.org/page"));
    EXPECT_FALSE(hfs::robots_allows(rules, "https://example.org/private"));
    EXPECT_TRUE(hfs::robots_allows(rules, "https://example.org/private/public"));
}

TEST(Robots, NormalizesUrlBeforeMatchingRules)
{
    EXPECT_FALSE(hfs::robots_allows("User-agent: *\nDisallow: /private\n",
                                    " HTTPS://EXAMPLE.ORG:443/open/../private#part "));
    EXPECT_FALSE(hfs::robots_allows("User-agent: *\nDisallow: /article$\n",
                                    "https://example.org/article#part"));
    EXPECT_FALSE(hfs::robots_allows("User-agent: *\nDisallow: /caf%C3%A9\n",
                                    "https://example.org/caf\xC3\xA9"));
}

TEST(Robots, DeniesInvalidUrlsEvenWhenRulesAreEmpty)
{
    for (const auto* url : {"", "/relative", "file:///page", "https://[bad",
                            "https://user:pass@example.org/"}) {
        SCOPED_TRACE(url);
        EXPECT_FALSE(hfs::robots_allows("", url));
    }
}

TEST(Robots, IndependentCallsDoNotShareMatcherState)
{
    constexpr auto url = "https://example.org/page";
    EXPECT_FALSE(hfs::robots_allows("User-agent: *\nDisallow: /\n", url));
    EXPECT_TRUE(hfs::robots_allows("", url));
    EXPECT_FALSE(hfs::robots_allows("User-agent: *\nDisallow: /\n", url));
}

TEST(RobotsCache, RejectsInvalidLifetimesAndMissingClock)
{
    using namespace std::chrono_literals;
    hfs::RobotsCacheOptions options;
    options.ttl = 25h;
    EXPECT_THROW(hfs::RobotsCache({}, options), std::invalid_argument);
    options.ttl = 0s;
    EXPECT_THROW(hfs::RobotsCache({}, options), std::invalid_argument);
    options.ttl = 10s;
    options.retry_after = 0s;
    EXPECT_THROW(hfs::RobotsCache({}, options), std::invalid_argument);
    options.retry_after = 11s;
    EXPECT_THROW(hfs::RobotsCache({}, options), std::invalid_argument);
    options.retry_after = 1s;
    EXPECT_THROW(hfs::RobotsCache({}, options, {}), std::invalid_argument);
}
