#include "hfs/url.hpp"

#include <gtest/gtest.h>

namespace {
constexpr std::string_view base = "https://example.org/dir/page?old=1#old";
}

TEST(Url, ResolvesRelativePaths)
{
    EXPECT_EQ(hfs::resolve_http_url(base, "../Article"), "https://example.org/Article");
    EXPECT_EQ(hfs::resolve_http_url(base, "/root"), "https://example.org/root");
    EXPECT_EQ(hfs::resolve_http_url(base, "child"), "https://example.org/dir/child");
}

TEST(Url, NormalizesHostSchemeDefaultPortAndDotSegments)
{
    EXPECT_EQ(hfs::resolve_http_url(base, " HTTPS://EXAMPLE.COM:443/a/../B#part "),
              "https://example.com/B");
    EXPECT_EQ(hfs::resolve_http_url(base, "http://EXAMPLE.COM:80"), "http://example.com/");
}

TEST(Url, PreservesQueryAndPathCase)
{
    EXPECT_EQ(hfs::resolve_http_url(base, "?b=2&a=1&a=3#part"),
              "https://example.org/dir/page?b=2&a=1&a=3");
    EXPECT_EQ(hfs::resolve_http_url(base, "/Some/Path?"), "https://example.org/Some/Path?");
}

TEST(Url, EmptyAndFragmentReferencesResolveToCurrentPage)
{
    EXPECT_EQ(hfs::resolve_http_url(base, std::string_view{}),
              "https://example.org/dir/page?old=1");
    EXPECT_EQ(hfs::resolve_http_url(base, "#section"), "https://example.org/dir/page?old=1");
}

TEST(Url, NetworkPathReferenceInheritsScheme)
{
    EXPECT_EQ(hfs::resolve_http_url(base, "//OTHER.org:8443/path"),
              "https://other.org:8443/path");
}

TEST(Url, RejectsOtherSchemesAndMalformedUrls)
{
    for (const auto* reference : {"mailto:a@example.org", "javascript:void(0)",
                                  "file:///tmp/page", "ftp://example.org/", "https://[bad"}) {
        SCOPED_TRACE(reference);
        EXPECT_FALSE(hfs::resolve_http_url(base, reference).has_value());
    }
}

TEST(Url, RequiresValidBaseAndRejectsCredentials)
{
    EXPECT_FALSE(hfs::resolve_http_url("relative/base", "/page"));
    EXPECT_FALSE(hfs::resolve_http_url("file:///tmp/", "https://example.org/"));
    EXPECT_FALSE(hfs::resolve_http_url(base, "https://user:pass@example.org/"));
    EXPECT_FALSE(hfs::resolve_http_url("https://user@example.org/", "/page"));
}
