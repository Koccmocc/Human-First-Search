#include "hfs/robots_loading.hpp"
#include "hfs/robots_cache.hpp"
#include "hfs/page_fetcher.hpp"
#include "hfs/url.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <array>
#include <stdexcept>

namespace {
// Existing policy scenarios run immediately; timing has dedicated scenarios below.
const hfs::HostDelayOptions immediate_requests{std::chrono::milliseconds{0}};
}

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

TEST_F(RobotsLoading, PageFetcherUsesCachedRulesAndBlocksPrivatePages)
{
    set_case("rules");
    hfs::PageFetcher fetcher{{"127.0.0.1"}, {}, immediate_requests};
    EXPECT_TRUE(std::holds_alternative<hfs::FetchResponse>(fetcher.fetch(base + "/public")));
    const auto blocked = fetcher.fetch(base + "/private/page");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(blocked));
    EXPECT_EQ(std::get<hfs::FetchError>(blocked).kind, hfs::FetchErrorKind::request_denied);
    set_case("empty");
    // Changing the server's rules has no effect until this session's cache expires.
    const auto cached = fetcher.fetch(base + "/private/page");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(cached));
    EXPECT_EQ(std::get<hfs::FetchError>(cached).kind, hfs::FetchErrorKind::request_denied);
}

TEST_F(RobotsLoading, PageFetcherChecksRobotsBeforeRedirectDestinations)
{
    set_case("rules");
    hfs::PageFetcher fetcher{{"127.0.0.1", "localhost"}, {}, immediate_requests};
    for (const auto* path : {"/to-private", "/page-cross-origin"}) {
        SCOPED_TRACE(path);
        const auto result = fetcher.fetch(base + path);
        ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(result));
        EXPECT_EQ(std::get<hfs::FetchError>(result).kind, hfs::FetchErrorKind::request_denied);
    }
    const auto allowed = fetcher.fetch(base + "/chain");
    const auto* fetched = std::get_if<hfs::FetchResponse>(&allowed);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->final_url, base + "/ok");
    const auto cross_origin = fetcher.fetch(base + "/page-cross-public");
    const auto* destination = std::get_if<hfs::FetchResponse>(&cross_origin);
    ASSERT_NE(destination, nullptr);
    EXPECT_EQ(destination->final_url, "http://localhost" + base.substr(base.rfind(':')) + "/public");
}

TEST_F(RobotsLoading, PageFetcherBlocksFailedRobotsAndAcceptsMissingRules)
{
    set_case("server-error");
    hfs::PageFetcher failed{{"127.0.0.1"}, {}, immediate_requests};
    const auto denied = failed.fetch(base + "/must-not-follow");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(denied));
    EXPECT_EQ(std::get<hfs::FetchError>(denied).kind, hfs::FetchErrorKind::request_denied);
    set_case("missing");
    hfs::PageFetcher missing{{"127.0.0.1"}, {}, immediate_requests};
    EXPECT_TRUE(std::holds_alternative<hfs::FetchResponse>(missing.fetch(base + "/public")));
}

TEST_F(RobotsLoading, PageFetcherRejectsOutOfScopeUrlsBeforeLoadingRules)
{
    hfs::PageFetcher empty{{}, {}, immediate_requests};
    const auto denied = empty.fetch(base + "/must-not-follow");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(denied));
    EXPECT_EQ(std::get<hfs::FetchError>(denied).kind, hfs::FetchErrorKind::request_denied);
    hfs::PageFetcher scoped{{"127.0.0.1"}, {}, immediate_requests};
    const auto outside = scoped.fetch("http://example.invalid/page");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(outside));
    EXPECT_EQ(std::get<hfs::FetchError>(outside).kind, hfs::FetchErrorKind::request_denied);
}

TEST_F(RobotsLoading, PageFetcherSharesDelayBetweenRobotsAndPageRedirects)
{
    using namespace std::chrono_literals;
    set_case("redirect");
    hfs::HostDelayOptions::Clock::time_point now{};
    std::vector<std::chrono::milliseconds> waits;
    hfs::HostDelayOptions timing{1s, [&] { return now; }, [&](auto duration) {
        waits.push_back(duration);
        now += duration;
    }};
    hfs::PageFetcher fetcher{{"127.0.0.1"}, {}, timing};
    const auto result = fetcher.fetch(base + "/chain");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchResponse>(result));
    // Two robots requests and three page requests share four intervals.
    EXPECT_EQ(waits, (std::vector<std::chrono::milliseconds>{1s, 1s, 1s, 1s}));
    EXPECT_EQ(now.time_since_epoch(), 4s);
    const auto denied = fetcher.fetch(base + "/private/page");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(denied));
    const auto outside = fetcher.fetch("https://example.invalid/page");
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(outside));
    EXPECT_EQ(waits.size(), 4U);
}
