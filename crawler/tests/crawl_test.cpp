#include "hfs/crawl.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>

class Crawl : public testing::Test {
protected:
    std::string base;
    hfs::CrawlOptions options;
    std::vector<hfs::CrawledPage> pages;
    const hfs::PageConsumer collect = [this](const auto& page) { pages.push_back(page); };

    void SetUp() override
    {
        const auto* address = std::getenv("HFS_TEST_BASE_URL");
        ASSERT_NE(address, nullptr) << "Run through CTest's local-server wrapper";
        base = address;
        options.timing.interval = std::chrono::milliseconds{0};
    }

    auto run(std::vector<std::string> seeds) -> hfs::CrawlSummary
    {
        return hfs::crawl(seeds, {"127.0.0.1"}, collect, options);
    }
};

TEST_F(Crawl, DiscoversLinksInBreadthFirstOrderUnderRobotsAndScopePolicy)
{
    const auto summary = run({base + "/crawl/start"});
    ASSERT_EQ(pages.size(), 4U);
    EXPECT_EQ(pages[0].url, base + "/crawl/start");
    EXPECT_EQ(pages[1].url, base + "/crawl/a");
    EXPECT_EQ(pages[2].url, base + "/crawl/b");
    EXPECT_EQ(pages[3].url, base + "/crawl/deep");
    EXPECT_EQ(pages[0].depth, 0U);
    EXPECT_EQ(pages[1].depth, 1U);
    EXPECT_EQ(pages[3].depth, 2U);
    EXPECT_EQ(pages[0].title, "Start");
    EXPECT_NE(pages[1].text.find("Alpha & beta"), std::string::npos);
    EXPECT_EQ(pages[0].http_status, 200);
    EXPECT_EQ(pages[0].content_type, "text/html; charset=utf-8");
    EXPECT_EQ(pages[0].requested_urls, (std::vector<std::string>{pages[0].url}));
    EXPECT_EQ(summary.pages, 4U);
    EXPECT_EQ(summary.targets_processed, 5U);
    EXPECT_EQ(summary.fetch_failures, 1U); // The forbidden page never reaches HTTP.
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
    EXPECT_EQ(summary.pending_urls, 0U);
}

TEST_F(Crawl, ResolvesLinksAtFinalUrlAndEmitsRedirectDestinationsOnce)
{
    const auto summary = run({base + "/crawl/alias", base + "/crawl/nested/page",
                              base + "/crawl/alias-two"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(pages[0].url, base + "/crawl/nested/page");
    EXPECT_EQ(pages[1].url, base + "/crawl/nested/child");
    EXPECT_EQ(pages[0].requested_urls, (std::vector<std::string>{base + "/crawl/alias",
                                                                pages[0].url}));
    EXPECT_EQ(pages[1].depth, 1U);
    EXPECT_EQ(summary.targets_processed, 4U);
    EXPECT_EQ(summary.duplicate_pages, 2U);
}

TEST_F(Crawl, LimitsPagesAndTargetsIndependently)
{
    options.max_pages = 1;
    auto summary = run({base + "/crawl/start"});
    EXPECT_EQ(summary.stop, hfs::CrawlStop::page_limit);
    EXPECT_EQ(summary.pages, 1U);
    EXPECT_EQ(summary.targets_processed, 1U);
    EXPECT_EQ(summary.pending_urls, 3U);

    pages.clear();
    options.max_pages = 10;
    options.max_targets = 1;
    summary = run({base + "/private/page", base + "/crawl/b"});
    EXPECT_EQ(summary.stop, hfs::CrawlStop::target_limit);
    EXPECT_EQ(summary.pages, 0U);
    EXPECT_EQ(summary.fetch_failures, 1U);
    EXPECT_EQ(summary.pending_urls, 1U);
    EXPECT_TRUE(pages.empty());
}

TEST_F(Crawl, AppliesDepthAndFrontierCapacityToDiscovery)
{
    options.frontier.max_depth = 1;
    auto summary = run({base + "/crawl/start"});
    EXPECT_EQ(summary.pages, 3U);
    EXPECT_EQ(summary.targets_processed, 4U);

    pages.clear();
    options.frontier.max_pending = 1;
    options.frontier.max_urls = 2;
    summary = run({base + "/crawl/start"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(pages[1].url, base + "/crawl/a");
    EXPECT_EQ(summary.targets_processed, 2U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

TEST_F(Crawl, IgnoresErrorBodiesAndNonHtmlResponsesForDiscovery)
{
    const auto summary = run({base + "/crawl/error", base + "/crawl/plain",
                              base + "/crawl/untyped", base + "/crawl/htmlish",
                              base + "/empty", base + "/no-location"});
    EXPECT_TRUE(pages.empty());
    EXPECT_EQ(summary.skipped_responses, 6U);
    EXPECT_EQ(summary.fetch_failures, 0U);
    EXPECT_EQ(summary.targets_processed, 6U);
}

TEST_F(Crawl, ContinuesAfterPolicyTransportAndRedirectFailures)
{
    const auto summary = run({base + "/private/page", base + "/partial",
                              base + "/loop-a", base + "/crawl/b"});
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0].title, "B");
    EXPECT_EQ(summary.fetch_failures, 3U);
    EXPECT_EQ(summary.targets_processed, 4U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

TEST_F(Crawl, HandlesEmptyAndRejectedSeedLists)
{
    EXPECT_EQ(run({}).targets_processed, 0U);
    const auto summary = run({"relative", "file:///unused", "http://example.invalid/page",
                              base + "/crawl/b", base + "/crawl/b#duplicate"});
    EXPECT_EQ(summary.rejected_seeds, 4U);
    EXPECT_EQ(summary.pages, 1U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

TEST_F(Crawl, ValidatesLimitsAndConsumerAndPropagatesConsumerExceptions)
{
    options.max_pages = 0;
    EXPECT_THROW(run({}), std::invalid_argument);
    options.max_pages = 1;
    options.max_targets = 0;
    EXPECT_THROW(run({}), std::invalid_argument);
    options.max_targets = 1;
    EXPECT_THROW((void)hfs::crawl({}, {}, {}, options), std::invalid_argument);

    const std::vector<std::string> seeds{base + "/crawl/b"};
    EXPECT_THROW((void)hfs::crawl(seeds, {"127.0.0.1"}, [](const auto&) {
        throw std::runtime_error{"Consumer failed"};
    }, options), std::runtime_error);
}
