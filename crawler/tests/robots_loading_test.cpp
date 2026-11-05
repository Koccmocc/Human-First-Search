#include "hfs/robots_loading.hpp"
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

    auto load_case(std::string_view scenario, const hfs::HttpOptions& options = {})
        -> hfs::RobotsDocument
    {
        const auto setup = hfs::http_get(base + "/robots-case/" + std::string{scenario});
        const auto* response = std::get_if<hfs::HttpResponse>(&setup);
        if (response == nullptr || response->status != 204) {
            throw std::runtime_error{"Could not configure robots fixture"};
        }
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
