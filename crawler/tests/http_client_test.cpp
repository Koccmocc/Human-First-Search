#include "hfs/http_client.hpp"
#include "hfs/http_fetch.hpp"
#include "hfs/url.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <array>
#include <utility>

class HttpClient : public testing::Test {
protected:
    std::string base;
    void SetUp() override
    {
        const auto* address = std::getenv("HFS_TEST_BASE_URL");
        ASSERT_NE(address, nullptr) << "Run through CTest's local-server wrapper";
        base = address;
    }
};

TEST_F(HttpClient, ReturnsBodyStatusAndContentType)
{
    const auto result = hfs::http_get(base + "/ok");
    const auto* response = std::get_if<hfs::HttpResponse>(&result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->status, 200);
    EXPECT_EQ(response->body, "<p>Hello</p>");
    EXPECT_EQ(response->content_type, "text/html; charset=utf-8");
}

TEST_F(HttpClient, ReturnsHttpErrorsAndRedirectsWithoutFollowing)
{
    for (const auto& [path, status] : {std::pair{"/missing", 404}, {"/redirect", 302}}) {
        SCOPED_TRACE(path);
        const auto result = hfs::http_get(base + path);
        const auto* response = std::get_if<hfs::HttpResponse>(&result);
        ASSERT_NE(response, nullptr);
        EXPECT_EQ(response->status, status);
        if (status == 302) {
            EXPECT_EQ(response->redirect_location, "/must-not-follow");
        }
    }
}

TEST_F(HttpClient, AcceptsEmptyBodyWithoutContentType)
{
    const auto result = hfs::http_get(base + "/empty");
    const auto* response = std::get_if<hfs::HttpResponse>(&result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->status, 204);
    EXPECT_TRUE(response->body.empty());
    EXPECT_TRUE(response->content_type.empty());
}

TEST_F(HttpClient, EnforcesBodyLimitWithoutContentLength)
{
    hfs::HttpOptions options;
    options.max_body_bytes = 127;
    const auto result = hfs::http_get(base + "/large", options);
    const auto* error = std::get_if<hfs::HttpError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, hfs::HttpErrorKind::body_limit);
    options.max_body_bytes = 128;
    const auto exact = hfs::http_get(base + "/large", options);
    const auto* response = std::get_if<hfs::HttpResponse>(&exact);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->body.size(), 128U);
}

TEST_F(HttpClient, EnforcesTotalTimeout)
{
    hfs::HttpOptions options;
    options.timeout_ms = 100;
    const auto result = hfs::http_get(base + "/slow", options);
    const auto* error = std::get_if<hfs::HttpError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, hfs::HttpErrorKind::timeout);
}

TEST_F(HttpClient, DoesNotReturnPartialTransfersAsCompleteResponses)
{
    const auto result = hfs::http_get(base + "/partial");
    const auto* error = std::get_if<hfs::HttpError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, hfs::HttpErrorKind::transport);
}

TEST_F(HttpClient, RejectsInvalidRequestsBeforeTransfer)
{
    const auto invalid_url = hfs::http_get("file:///unused");
    ASSERT_TRUE(std::holds_alternative<hfs::HttpError>(invalid_url));
    EXPECT_EQ(std::get<hfs::HttpError>(invalid_url).kind, hfs::HttpErrorKind::invalid_request);
    hfs::HttpOptions options;
    options.timeout_ms = 0;
    const auto invalid_options = hfs::http_get(base + "/ok", options);
    ASSERT_TRUE(std::holds_alternative<hfs::HttpError>(invalid_options));
    EXPECT_EQ(std::get<hfs::HttpError>(invalid_options).kind, hfs::HttpErrorKind::invalid_request);
}

TEST_F(HttpClient, ReturnsRelativeLocationForCallerResolution)
{
    const auto result = hfs::http_get(base + "/relative");
    const auto* response = std::get_if<hfs::HttpResponse>(&result);
    ASSERT_NE(response, nullptr);
    ASSERT_TRUE(response->redirect_location.has_value());
    EXPECT_EQ(*response->redirect_location, "../must-not-follow?x=1#part");
    EXPECT_EQ(hfs::resolve_http_url(base + "/relative", *response->redirect_location),
              base + "/must-not-follow?x=1");
}

TEST_F(HttpClient, ReturnsAbsoluteLocationWithoutFetchingIt)
{
    const auto result = hfs::http_get(base + "/absolute");
    const auto* response = std::get_if<hfs::HttpResponse>(&result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response->status, 303);
    EXPECT_EQ(response->redirect_location, "https://example.invalid/not-fetched");
}

TEST_F(HttpClient, PreservesRejectedTargetsForCallerValidation)
{
    for (const auto* path : {"/other-scheme", "/credentials"}) {
        SCOPED_TRACE(path);
        const auto result = hfs::http_get(base + path);
        const auto* response = std::get_if<hfs::HttpResponse>(&result);
        ASSERT_NE(response, nullptr);
        ASSERT_TRUE(response->redirect_location.has_value());
        EXPECT_FALSE(hfs::resolve_http_url(base + path, *response->redirect_location));
    }
}

TEST_F(HttpClient, DoesNotTreatEveryLocationAsARedirect)
{
    for (const auto* path : {"/ok", "/no-location", "/ok-location", "/not-modified"}) {
        SCOPED_TRACE(path);
        const auto result = hfs::http_get(base + path);
        const auto* response = std::get_if<hfs::HttpResponse>(&result);
        ASSERT_NE(response, nullptr);
        EXPECT_FALSE(response->redirect_location.has_value());
    }
}

TEST_F(HttpClient, FollowsBoundedChainWithPermissionBeforeEveryRequest)
{
    const auto host = hfs::http_url_host(base);
    ASSERT_TRUE(host.has_value());
    const std::array<std::string_view, 1> hosts{*host};
    std::vector<std::string> checked;
    const auto result = hfs::fetch_http(base + "/chain", [&](std::string_view url) {
        checked.emplace_back(url);
        return hfs::is_allowed_host(url, hosts);
    }, {}, 2);
    const auto* fetched = std::get_if<hfs::FetchResponse>(&result);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->final_url, base + "/ok");
    EXPECT_EQ(fetched->response.status, 200);
    EXPECT_EQ(checked, (std::vector<std::string>{base + "/chain", base + "/hop", base + "/ok"}));
    EXPECT_EQ(fetched->requested_urls, checked);
}

TEST_F(HttpClient, DeniesInitialAndRedirectRequestsBeforeTransfer)
{
    const auto denied = hfs::fetch_http(base + "/must-not-follow", {});
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(denied));
    EXPECT_EQ(std::get<hfs::FetchError>(denied).kind, hfs::FetchErrorKind::request_denied);
    const auto host = hfs::http_url_host(base);
    ASSERT_TRUE(host.has_value());
    const std::array<std::string_view, 1> hosts{*host};
    const auto outside = hfs::fetch_http(base + "/absolute", [&](std::string_view url) {
        return hfs::is_allowed_host(url, hosts);
    });
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(outside));
    EXPECT_EQ(std::get<hfs::FetchError>(outside).kind, hfs::FetchErrorKind::request_denied);
    const auto blocked_path = hfs::fetch_http(base + "/redirect", [&](std::string_view url) {
        return url != base + "/must-not-follow";
    });
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(blocked_path));
    EXPECT_EQ(std::get<hfs::FetchError>(blocked_path).kind, hfs::FetchErrorKind::request_denied);
}

TEST_F(HttpClient, RedirectLimitCountsTransitions)
{
    const auto permit = [](std::string_view) { return true; };
    EXPECT_TRUE(std::holds_alternative<hfs::FetchResponse>(hfs::fetch_http(base + "/ok", permit, {}, 0)));
    for (const auto limit : {0U, 1U}) {
        const auto result = hfs::fetch_http(base + "/chain", permit, {}, limit);
        ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(result));
        EXPECT_EQ(std::get<hfs::FetchError>(result).kind, hfs::FetchErrorKind::redirect_limit);
    }
}

TEST_F(HttpClient, RejectsRedirectCyclesBeforeRepeatingRequests)
{
    std::vector<std::string> checked;
    const auto result = hfs::fetch_http(base + "/loop-a", [&](std::string_view url) {
        checked.emplace_back(url);
        return true;
    });
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(result));
    EXPECT_EQ(std::get<hfs::FetchError>(result).kind, hfs::FetchErrorKind::redirect_loop);
    EXPECT_EQ(checked, (std::vector<std::string>{base + "/loop-a", base + "/loop-b"}));
}

TEST_F(HttpClient, RejectsUnsupportedRedirectsAndPreservesTransportErrors)
{
    const auto permit = [](std::string_view) { return true; };
    for (const auto* path : {"/other-scheme", "/credentials"}) {
        const auto result = hfs::fetch_http(base + path, permit);
        ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(result));
        EXPECT_EQ(std::get<hfs::FetchError>(result).kind, hfs::FetchErrorKind::invalid_url);
    }
    const auto partial = hfs::fetch_http(base + "/to-partial", permit);
    ASSERT_TRUE(std::holds_alternative<hfs::HttpError>(partial));
    EXPECT_EQ(std::get<hfs::HttpError>(partial).kind, hfs::HttpErrorKind::transport);
}
