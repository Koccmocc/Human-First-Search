#include <curl/curl.h>
#include <gtest/gtest.h>

#include <memory>
#include <string_view>

TEST(HttpTransport, InitializesWithHttpAndHttpsSupport)
{
    ASSERT_EQ(curl_global_init(CURL_GLOBAL_DEFAULT), CURLE_OK);
    struct Cleanup {
        ~Cleanup() { curl_global_cleanup(); }
    } cleanup;

    // An easy handle is released before global cleanup, even after a failed assertion.
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>
        handle{curl_easy_init(), &curl_easy_cleanup};
    ASSERT_NE(handle, nullptr);
    const auto* info = curl_version_info(CURLVERSION_NOW);
    ASSERT_NE(info, nullptr);
    EXPECT_NE(info->features & CURL_VERSION_SSL, 0);
    ASSERT_NE(info->ssl_version, nullptr);
    ASSERT_NE(info->protocols, nullptr);

    bool http{};
    bool https{};
    for (auto protocol = info->protocols; *protocol != nullptr; ++protocol) {
        http = http || std::string_view{*protocol} == "http";
        https = https || std::string_view{*protocol} == "https";
    }
    EXPECT_TRUE(http);
    EXPECT_TRUE(https);
#ifdef _WIN32
    EXPECT_NE(std::string_view{info->ssl_version}.find("Schannel"), std::string_view::npos);
#endif
    // This verifies linkage and capabilities, without making a network request.
}
