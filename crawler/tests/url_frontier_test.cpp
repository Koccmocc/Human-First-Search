#include "hfs/url_frontier.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

using hfs::EnqueueResult;

TEST(UrlFrontier, PreservesFifoOrderAndTracksDepth)
{
    hfs::UrlFrontier frontier{{"example.org"}};
    EXPECT_FALSE(frontier.pop());
    ASSERT_EQ(frontier.add_seed("https://example.org/first"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://example.org/second"), EnqueueResult::added);
    const auto first = frontier.pop();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->url, "https://example.org/first");
    EXPECT_EQ(first->depth, 0U);
    ASSERT_EQ(frontier.add_link(*first, "child"), EnqueueResult::added);
    const auto second = frontier.pop();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->url, "https://example.org/second");
    const auto child = frontier.pop();
    ASSERT_TRUE(child);
    EXPECT_EQ(child->url, "https://example.org/child");
    EXPECT_EQ(child->depth, 1U);
    EXPECT_EQ(frontier.size(), 0U);
    EXPECT_EQ(frontier.accepted_count(), 3U);
}

TEST(UrlFrontier, DeduplicatesCanonicalUrlsEvenAfterPopping)
{
    hfs::UrlFrontier frontier{{"example.org"}};
    ASSERT_EQ(frontier.add_seed("HTTPS://EXAMPLE.ORG:443/page#one"), EnqueueResult::added);
    EXPECT_EQ(frontier.add_seed("https://example.org/page#two"), EnqueueResult::duplicate);
    const auto page = frontier.pop();
    ASSERT_TRUE(page);
    EXPECT_EQ(frontier.add_link(*page, "#three"), EnqueueResult::duplicate);
    EXPECT_EQ(frontier.add_seed(page->url), EnqueueResult::duplicate);
    EXPECT_EQ(frontier.add_seed("https://example.org/Page"), EnqueueResult::added);
    EXPECT_EQ(frontier.add_seed("https://example.org/page?q=1"), EnqueueResult::added);
}

TEST(UrlFrontier, QueueFullDoesNotMarkRejectedUrlsAsKnown)
{
    hfs::UrlFrontier frontier{{"example.org"}, {1, 3, 3}};
    ASSERT_EQ(frontier.add_seed("https://example.org/a"), EnqueueResult::added);
    EXPECT_EQ(frontier.add_seed("https://example.org/b"), EnqueueResult::queue_full);
    EXPECT_EQ(frontier.accepted_count(), 1U);
    ASSERT_TRUE(frontier.pop());
    EXPECT_EQ(frontier.add_seed("https://example.org/b"), EnqueueResult::added);
}

TEST(UrlFrontier, TotalUrlBudgetRemainsBoundedAfterPopping)
{
    hfs::UrlFrontier frontier{{"example.org"}, {2, 2, 3}};
    ASSERT_EQ(frontier.add_seed("https://example.org/a"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://example.org/b"), EnqueueResult::added);
    ASSERT_TRUE(frontier.pop());
    EXPECT_EQ(frontier.add_seed("https://example.org/c"), EnqueueResult::url_limit);
    EXPECT_EQ(frontier.add_seed("https://example.org/a"), EnqueueResult::duplicate);
    EXPECT_EQ(frontier.accepted_count(), 2U);
}

TEST(UrlFrontier, DepthLimitIncludesSeedsAndPreventsOverflow)
{
    hfs::UrlFrontier seeds_only{{"example.org"}, {10, 10, 0}};
    ASSERT_EQ(seeds_only.add_seed("https://example.org/"), EnqueueResult::added);
    const auto seed = seeds_only.pop();
    ASSERT_TRUE(seed);
    EXPECT_EQ(seeds_only.add_link(*seed, "/child"), EnqueueResult::depth_limit);
    hfs::UrlFrontier frontier{{"example.org"}, {10, 10, 1}};
    ASSERT_EQ(frontier.add_link(*seed, "/child"), EnqueueResult::added);
    const auto child = frontier.pop();
    ASSERT_TRUE(child);
    EXPECT_EQ(frontier.add_link(*child, "/grandchild"), EnqueueResult::depth_limit);
    EXPECT_EQ(frontier.add_link({seed->url, std::numeric_limits<std::size_t>::max()}, "/overflow"),
              EnqueueResult::depth_limit);
}

TEST(UrlFrontier, RejectsInvalidAndOutOfScopeCandidatesWithoutAdmission)
{
    hfs::UrlFrontier frontier{{"example.org"}};
    EXPECT_EQ(frontier.add_seed("file:///page"), EnqueueResult::invalid_url);
    EXPECT_EQ(frontier.add_seed("/relative"), EnqueueResult::invalid_url);
    EXPECT_EQ(frontier.add_seed("https://example.org.evil.com/"), EnqueueResult::out_of_scope);
    EXPECT_EQ(frontier.add_seed("https://sub.example.org/"), EnqueueResult::out_of_scope);
    EXPECT_EQ(frontier.add_link({"https://example.org/dir/page", 0}, "//other.org/"),
              EnqueueResult::out_of_scope);
    EXPECT_EQ(frontier.size(), 0U);
    EXPECT_EQ(frontier.accepted_count(), 0U);
    hfs::UrlFrontier empty{{}};
    EXPECT_EQ(empty.add_seed("https://example.org/"), EnqueueResult::out_of_scope);
}

TEST(UrlFrontier, RequiresPositiveQueueAndTotalLimits)
{
    EXPECT_THROW((hfs::UrlFrontier{{"example.org"}, {0, 10, 3}}), std::invalid_argument);
    EXPECT_THROW((hfs::UrlFrontier{{"example.org"}, {10, 0, 3}}), std::invalid_argument);
}
