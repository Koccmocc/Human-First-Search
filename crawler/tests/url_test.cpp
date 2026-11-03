#include "hfs/url.hpp"

#include <gtest/gtest.h>
#include <array>

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

TEST(Url, ExtractsCanonicalHostWithoutPort)
{
    EXPECT_EQ(hfs::http_url_host("HTTPS://EXAMPLE.ORG:8443/path?query#part"), "example.org");
    EXPECT_EQ(hfs::http_url_host("http://[::1]:8080/"), "[::1]");
    EXPECT_EQ(hfs::http_url_host("https://127.0.0.1/"), "127.0.0.1");
    EXPECT_EQ(hfs::http_url_host("https://caf%C3%A9.example/"), "xn--caf-dma.example");
}

TEST(Url, HostExtractionRejectsInvalidOrUnusableUrls)
{
    EXPECT_FALSE(hfs::http_url_host("/relative"));
    EXPECT_FALSE(hfs::http_url_host("https://[bad"));
    EXPECT_FALSE(hfs::http_url_host("file:///tmp/page"));
    EXPECT_FALSE(hfs::http_url_host("https://user:pass@example.org/"));
}

TEST(Url, AllowlistMatchesExactCanonicalHost)
{
    constexpr std::array<std::string_view, 2> hosts{"example.org", "docs.example.net"};
    EXPECT_TRUE(hfs::is_allowed_host("https://EXAMPLE.ORG/page", hosts));
    EXPECT_TRUE(hfs::is_allowed_host("http://docs.example.net/", hosts));
    EXPECT_TRUE(hfs::is_allowed_host("https://example.org:8443/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://sub.example.org/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://example.org.evil.com/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://notexample.org/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://example.org./", hosts));
}

TEST(Url, AllowlistDoesNotMatchHostNamesInOtherUrlComponents)
{
    constexpr std::array<std::string_view, 1> hosts{"example.org"};
    EXPECT_FALSE(hfs::is_allowed_host("https://evil.com/example.org?host=example.org", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://example.org@evil.com/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://user@example.org/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("javascript:example.org", hosts));
}

TEST(Url, EmptyAllowlistDeniesAllHosts)
{
    EXPECT_FALSE(hfs::is_allowed_host("https://example.org/", {}));
}

TEST(Url, SubdomainsMustBeExplicitlyAllowed)
{
    constexpr std::array<std::string_view, 1> hosts{"sub.example.org"};
    EXPECT_TRUE(hfs::is_allowed_host("https://sub.example.org/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://example.org/", hosts));
    EXPECT_FALSE(hfs::is_allowed_host("https://other.sub.example.org/", hosts));
}

TEST(Url, RobotsFileLivesAtRootRegardlessOfPagePathAndQuery)
{
    for (const auto* page : {"https://example.org/dir/article?x=1#part",
                             "https://example.org/", "https://example.org/ROBOTS.TXT?x=1"}) {
        SCOPED_TRACE(page);
        EXPECT_EQ(hfs::robots_txt_url(page), "https://example.org/robots.txt");
    }
}

TEST(Url, RobotsLocationKeepsSchemeAndNonDefaultPortDistinct)
{
    EXPECT_EQ(hfs::robots_txt_url("https://example.org:8443/article"),
              "https://example.org:8443/robots.txt");
    EXPECT_EQ(hfs::robots_txt_url("http://example.org:8080/article"),
              "http://example.org:8080/robots.txt");
    EXPECT_NE(hfs::robots_txt_url("http://example.org/"), hfs::robots_txt_url("https://example.org/"));
    EXPECT_NE(hfs::robots_txt_url("https://example.org/"),
              hfs::robots_txt_url("https://example.org:8443/"));
    // Equivalent spellings must share one cache entry.
    EXPECT_EQ(hfs::robots_txt_url("HTTPS://EXAMPLE.ORG:443/a"),
              hfs::robots_txt_url("https://example.org/b"));
    EXPECT_EQ(hfs::robots_txt_url("http://example.org:80/a"),
              hfs::robots_txt_url("http://example.org/b"));
}

TEST(Url, RobotsLocationHandlesIpv6AndInternationalHostNames)
{
    EXPECT_EQ(hfs::robots_txt_url("http://[::1]:8080/article"), "http://[::1]:8080/robots.txt");
    EXPECT_EQ(hfs::robots_txt_url("https://caf%C3%A9.example/article"),
              "https://xn--caf-dma.example/robots.txt");
}

TEST(Url, RobotsLocationRejectsInvalidInputsInsteadOfGuessingAnOrigin)
{
    for (const auto* page : {"", "/relative", "example.org/path", "file:///page",
                             "https://[bad", "https://user:pass@example.org/"}) {
        SCOPED_TRACE(page);
        EXPECT_FALSE(hfs::robots_txt_url(page));
    }
}
