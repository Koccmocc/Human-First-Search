#include "hfs/url_frontier.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <utility>

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

TEST(UrlFrontier, RoundRobinsHostsInOrderOfFirstAdmission)
{
    hfs::UrlFrontier frontier{{"b.org", "a.org", "c.org"}};
    for (const auto* url : {"https://a.org/1", "https://a.org/2", "https://a.org/3",
                            "https://b.org/1", "https://b.org/2", "https://c.org/1"}) {
        ASSERT_EQ(frontier.add_seed(url), EnqueueResult::added);
    }
    EXPECT_EQ(frontier.size(), 6U);
    for (const auto* expected : {"https://a.org/1", "https://b.org/1", "https://c.org/1",
                                 "https://a.org/2", "https://b.org/2", "https://a.org/3"}) {
        const auto target = frontier.pop();
        ASSERT_TRUE(target);
        EXPECT_EQ(target->url, expected);
    }
    EXPECT_EQ(frontier.size(), 0U);
    EXPECT_EQ(frontier.accepted_count(), 6U);
    EXPECT_FALSE(frontier.pop());
}

TEST(UrlFrontier, KeepsShallowerTargetsAheadOfRoundRobinChildren)
{
    hfs::UrlFrontier frontier{{"a.org", "b.org"}};
    ASSERT_EQ(frontier.add_link({"https://b.org/parent", 1}, "/grandchild"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_link({"https://a.org/parent", 0}, "/child-1"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_link({"https://a.org/parent", 0}, "/child-2"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_link({"https://b.org/parent", 0}, "/child"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://b.org/seed"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://a.org/seed"), EnqueueResult::added);
    const std::pair<std::string_view, std::size_t> expected[]{
        {"https://b.org/seed", 0}, {"https://a.org/seed", 0},
        {"https://a.org/child-1", 1}, {"https://b.org/child", 1},
        {"https://a.org/child-2", 1}, {"https://b.org/grandchild", 2}};
    for (const auto& [url, depth] : expected) {
        const auto target = frontier.pop();
        ASSERT_TRUE(target);
        EXPECT_EQ(target->url, url);
        EXPECT_EQ(target->depth, depth);
    }
}

TEST(UrlFrontier, RejoinsDrainedHostsAtEndOfCurrentDepthRing)
{
    hfs::UrlFrontier frontier{{"a.org", "b.org", "c.org"}};
    ASSERT_EQ(frontier.add_seed("https://a.org/1"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://a.org/2"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://b.org/1"), EnqueueResult::added);
    ASSERT_TRUE(frontier.pop()); // A1 rotates A behind B.
    ASSERT_EQ(frontier.add_seed("https://b.org/2"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://c.org/1"), EnqueueResult::added);
    const auto second = frontier.pop();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->url, "https://b.org/1");
    const auto third = frontier.pop();
    ASSERT_TRUE(third);
    EXPECT_EQ(third->url, "https://a.org/2"); // A drains completely.
    ASSERT_EQ(frontier.add_seed("https://a.org/3"), EnqueueResult::added);
    for (const auto* expected : {"https://c.org/1", "https://b.org/2", "https://a.org/3"}) {
        const auto target = frontier.pop();
        ASSERT_TRUE(target);
        EXPECT_EQ(target->url, expected);
    }
    EXPECT_FALSE(frontier.pop());
}

TEST(UrlFrontier, GroupsCanonicalHostsAcrossSchemesAndPorts)
{
    hfs::UrlFrontier frontier{{"a.org", "sub.a.org"}};
    ASSERT_EQ(frontier.add_seed("HTTPS://A.ORG:443/1#part"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("http://a.org:8080/2"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://sub.a.org/1"), EnqueueResult::added);
    for (const auto* expected : {"https://a.org/1", "https://sub.a.org/1", "http://a.org:8080/2"}) {
        const auto target = frontier.pop();
        ASSERT_TRUE(target);
        EXPECT_EQ(target->url, expected);
    }
}

TEST(UrlFrontier, AppliesGlobalLimitsAcrossHostQueuesWithoutRememberingRejectedCandidates)
{
    hfs::UrlFrontier frontier{{"a.org", "b.org"}, {2, 3, 1}};
    ASSERT_EQ(frontier.add_seed("https://a.org/1"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_seed("https://a.org/2"), EnqueueResult::added);
    EXPECT_EQ(frontier.add_seed("https://b.org/1"), EnqueueResult::queue_full);
    EXPECT_EQ(frontier.accepted_count(), 2U);
    ASSERT_TRUE(frontier.pop());
    ASSERT_EQ(frontier.add_seed("https://b.org/1"), EnqueueResult::added);
    EXPECT_EQ(frontier.size(), 2U);
    EXPECT_EQ(frontier.accepted_count(), 3U);
    EXPECT_EQ(frontier.add_seed("https://b.org/2"), EnqueueResult::url_limit);
    ASSERT_TRUE(frontier.pop());
    ASSERT_TRUE(frontier.pop());
    EXPECT_EQ(frontier.add_seed("https://b.org/1"), EnqueueResult::duplicate);
    EXPECT_EQ(frontier.add_seed("https://b.org/2"), EnqueueResult::url_limit);
}

TEST(UrlFrontier, AssignsDiscoveredLinksToDestinationHost)
{
    hfs::UrlFrontier frontier{{"a.org", "b.org"}};
    const hfs::CrawlTarget parent{"https://a.org/page", 0};
    ASSERT_EQ(frontier.add_link(parent, "https://b.org/1"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_link(parent, "https://b.org/2"), EnqueueResult::added);
    ASSERT_EQ(frontier.add_link(parent, "/1"), EnqueueResult::added);
    for (const auto* expected : {"https://b.org/1", "https://a.org/1", "https://b.org/2"}) {
        const auto target = frontier.pop();
        ASSERT_TRUE(target);
        EXPECT_EQ(target->url, expected);
        EXPECT_EQ(target->depth, 1U);
    }
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
