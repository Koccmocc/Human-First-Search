#include "hfs/export_filter.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

TEST(ExportFilter, MatchesExactPathsOrDirectorySubtreesWithoutQueryConfusion)
{
    const std::vector<std::string> rules{"HTTPS://EXAMPLE.org:443/jobs/*", "https://example.org/genindex.html"};
    const hfs::ExportFilter filter{rules};
    for (const auto* url : {"https://example.org/jobs/", "https://example.org/jobs/a?sort=new",
                            "https://example.org/genindex.html?theme=dark"}) {
        EXPECT_TRUE(filter.excludes(url)) << url;
    }
    for (const auto* url : {"https://example.org/jobs", "https://example.org/jobshop/a",
                            "https://example.org/genindex.html.extra", "https://example.org/Jobs/a",
                            "https://sub.example.org/jobs/a", "https://example.org.evil/jobs/a",
                            "http://example.org/jobs/a", "https://example.org:8443/jobs/a",
                            "https://example.org/?next=https://example.org/jobs/a",
                            "https://other.org/jobs/?host=example.org"}) {
        EXPECT_FALSE(filter.excludes(url)) << url;
    }
    EXPECT_FALSE(hfs::ExportFilter{std::vector<std::string>{}}.excludes("https://example.org/jobs/a"));
}

TEST(ExportFilter, NormalizesAndOwnsPatternsWithoutDecodingReservedPathBytes)
{
    std::vector<std::string> patterns{"https://EXAMPLE.org/a/../jobs/*", "https://example.org/a%2Fb"};
    const hfs::ExportFilter filter{patterns};
    patterns.clear();
    EXPECT_TRUE(filter.excludes("https://example.org/jobs/child"));
    EXPECT_TRUE(filter.excludes("https://example.org/a%2Fb"));
    EXPECT_FALSE(filter.excludes("https://example.org/a/b"));
    EXPECT_EQ(hfs::normalize_export_pattern("HTTPS://EXAMPLE.org:443/jobs/*"), "https://example.org/jobs/*");
}

TEST(ExportFilter, RejectsAmbiguousPatterns)
{
    for (const auto* pattern : {"", "/jobs/*", "file:///jobs/*", "https://u:p@example.org/jobs/*",
        "https://example.org/jobs?sort=new", "https://example.org/jobs#title", "https://example.org/job*",
        "https://example.org/*/child", "https://example.org/jobs/**", "https://example.org/a b",
        "https://example.org/\\jobs/*"}) {
        EXPECT_FALSE(hfs::normalize_export_pattern(pattern)) << pattern;
        EXPECT_THROW((hfs::ExportFilter{std::vector<std::string>{pattern}}), std::invalid_argument);
    }
}
