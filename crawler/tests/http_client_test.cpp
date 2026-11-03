#include "hfs/http_client.hpp"
#include "hfs/url.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
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
