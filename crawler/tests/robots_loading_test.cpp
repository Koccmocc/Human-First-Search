#include "hfs/robots_loading.hpp"
#include "hfs/robots_cache.hpp"
#include "hfs/url.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <array>
#include <stdexcept>

class RobotsLoading : public testing::Test {
protected:
    std::string base;
    void SetUp() override
    {
        const auto* address = std::getenv("HFS_TEST_BASE_URL");
        ASSERT_NE(address, nullptr);
        base = address;
    }

    void set_case(std::string_view scenario)
    {
        const auto setup = hfs::http_get(base + "/robots-case/" + std::string{scenario});
        const auto* response = std::get_if<hfs::HttpResponse>(&setup);
        if (response == nullptr || response->status != 204) {
            throw std::runtime_error{"Could not configure robots fixture"};
        }
    }

    auto load_case(std::string_view scenario, const hfs::HttpOptions& options = {})
        -> hfs::RobotsDocument
    {
        set_case(scenario);
        return hfs::load_robots(base + "/nested/page?x=1", [](std::string_view url) {
            constexpr std::array<std::string_view, 2> hosts{"127.0.0.1", "localhost"};
            return hfs::is_allowed_host(url, hosts);
        }, options);
    }
};

TEST_F(RobotsLoading, LoadsRulesAndBindsThemToTheirInitialOrigin)
{
    for (const auto* scenario : {"rules", "redirect", "cross-origin"}) {
        SCOPED_TRACE(scenario);
        const auto document = load_case(scenario);
        EXPECT_EQ(document.state, hfs::RobotsState::rules);
        EXPECT_EQ(document.source_url, base + "/robots.txt");
        EXPECT_TRUE(document.allows(base + "/public"));
        EXPECT_FALSE(document.allows(base + "/private"));
        EXPECT_FALSE(document.allows("https://example.org/public"));
        EXPECT_FALSE(document.allows("https" + base.substr(4) + "/public"));
        EXPECT_FALSE(document.allows("http://127.0.0.1:1/public"));
        const auto alias = "http://localhost" + base.substr(base.rfind(':'));
        EXPECT_FALSE(document.allows(alias + "/public"));
    }
}

TEST_F(RobotsLoading, SuccessfullyLoadedEmptyRulesPermitAccess)
{
    for (const auto* scenario : {"empty", "no-content"}) {
        const auto document = load_case(scenario);
        EXPECT_EQ(document.state, hfs::RobotsState::rules);
        EXPECT_TRUE(document.text.empty());
        EXPECT_TRUE(document.allows(base + "/page"));
    }
}

TEST_F(RobotsLoading, MissingOrGoneFilesPermitAccess)
{
    for (const auto* scenario : {"missing", "gone"}) {
        const auto document = load_case(scenario);
        EXPECT_EQ(document.state, hfs::RobotsState::unavailable);
        EXPECT_TRUE(document.allows(base + "/page"));
        EXPECT_FALSE(document.allows("https://example.org/page"));
    }
}

TEST_F(RobotsLoading, OtherHttpStatusesBlockAccess)
{
    for (const auto* scenario : {"forbidden", "rate-limit", "server-error", "not-modified"}) {
        SCOPED_TRACE(scenario);
        const auto document = load_case(scenario);
        EXPECT_EQ(document.state, hfs::RobotsState::unreachable);
        EXPECT_NE(document.http_status, 0);
        EXPECT_FALSE(document.allows(base + "/page"));
        EXPECT_FALSE(document.detail.empty());
    }
}

TEST_F(RobotsLoading, FailedTransfersAndRedirectsBlockAccess)
{
    for (const auto* scenario : {"slow", "large", "loop", "bad-redirect"}) {
        SCOPED_TRACE(scenario);
        hfs::HttpOptions options;
        options.timeout_ms = std::string_view{scenario} == "slow" ? 100 : 3000;
        options.max_body_bytes = 500 * 1024;
        const auto document = load_case(scenario, options);
        EXPECT_EQ(document.state, hfs::RobotsState::unreachable);
        EXPECT_TRUE(document.text.empty());
        EXPECT_FALSE(document.allows(base + "/page"));
        EXPECT_FALSE(document.detail.empty());
    }
}

TEST_F(RobotsLoading, InvalidRequestsAndMissingPermissionBlockAccess)
{
    const auto invalid = hfs::load_robots("file:///unused", {});
    EXPECT_EQ(invalid.state, hfs::RobotsState::unreachable);
    EXPECT_FALSE(invalid.allows(base + "/page"));
    const auto denied = hfs::load_robots(base + "/page", {});
    EXPECT_EQ(denied.state, hfs::RobotsState::unreachable);
    EXPECT_FALSE(denied.detail.empty());
    hfs::HttpOptions options;
    options.max_body_bytes = 1;
    const auto too_small = hfs::load_robots(base + "/page", {}, options);
    EXPECT_FALSE(too_small.allows(base + "/page"));
}

TEST_F(RobotsLoading, CacheReusesOriginRulesAndRefreshesAtExpiry)
{
    using namespace std::chrono_literals;
    hfs::RobotsCache::Clock::time_point now{};
    int requests{};
    hfs::RobotsCacheOptions options;
    options.ttl = 10s;
    options.retry_after = 2s;
    hfs::RobotsCache cache{[&](std::string_view) { ++requests; return true; }, options, [&] { return now; }};
    set_case("rules");
    auto first = cache.get(base + "/public");
    EXPECT_TRUE(first.allows(base + "/public"));
    first.text.clear();
    set_case("empty");
    now += 9s;
    EXPECT_FALSE(cache.get(base + "/another?x=1").allows(base + "/private"));
    EXPECT_EQ(requests, 1);
    now += 1s;
    EXPECT_TRUE(cache.get(base + "/private").allows(base + "/private"));
    EXPECT_EQ(requests, 2);
}

TEST_F(RobotsLoading, CacheRetriesErrorsSoonerAndBlocksAfterFailedRefresh)
{
    using namespace std::chrono_literals;
    hfs::RobotsCache::Clock::time_point now{};
    int requests{};
    hfs::RobotsCacheOptions options;
    options.ttl = 10s;
    options.retry_after = 2s;
    hfs::RobotsCache cache{[&](std::string_view) { ++requests; return true; }, options, [&] { return now; }};
    set_case("rules");
    EXPECT_TRUE(cache.get(base).allows(base + "/public"));
    set_case("server-error");
    now += 10s;
    EXPECT_EQ(cache.get(base).state, hfs::RobotsState::unreachable);
    set_case("empty");
    now += 1s;
    EXPECT_FALSE(cache.get(base).allows(base + "/public"));
    EXPECT_EQ(requests, 2);
    now += 1s;
    EXPECT_TRUE(cache.get(base).allows(base + "/public"));
    EXPECT_EQ(requests, 3);
}

TEST_F(RobotsLoading, CacheSeparatesOriginsAndClearForcesReload)
{
    int requests{};
    hfs::RobotsCache cache{[&](std::string_view) { ++requests; return true; }};
    set_case("rules");
    EXPECT_EQ(cache.get(base).state, hfs::RobotsState::rules);
    const auto alias = "http://localhost" + base.substr(base.rfind(':'));
    EXPECT_EQ(cache.get(alias).state, hfs::RobotsState::rules);
    EXPECT_EQ(requests, 2);
    EXPECT_FALSE(cache.get("file:///unused").allows(base));
    EXPECT_EQ(requests, 2);
    set_case("empty");
    cache.clear();
    EXPECT_TRUE(cache.get(base).allows(base + "/private"));
    EXPECT_EQ(requests, 3);
}

TEST_F(RobotsLoading, CacheRetainsMissingFileStateUntilNormalExpiry)
{
    using namespace std::chrono_literals;
    hfs::RobotsCache::Clock::time_point now{};
    int requests{};
    hfs::RobotsCacheOptions options;
    options.ttl = 10s;
    options.retry_after = 2s;
    hfs::RobotsCache cache{[&](std::string_view) { ++requests; return true; }, options, [&] { return now; }};
    set_case("missing");
    EXPECT_EQ(cache.get(base).state, hfs::RobotsState::unavailable);
    set_case("rules");
    now += 2s;
    EXPECT_TRUE(cache.get(base).allows(base + "/private"));
    EXPECT_EQ(requests, 1);
    now += 8s;
    EXPECT_FALSE(cache.get(base).allows(base + "/private"));
    EXPECT_EQ(requests, 2);
}
