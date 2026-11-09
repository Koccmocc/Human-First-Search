#include "hfs/crawl.hpp"
#include "hfs/page_identity.hpp"

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

TEST_F(Crawl, DeliversStableIdentitiesFromFinalUrlAndExtractedText)
{
    (void)run({base + "/crawl/alias"});
    ASSERT_EQ(pages.size(), 2U);
    const auto expected = hfs::make_page_identity(base + "/crawl/nested/page", "Child");
    const auto alias = hfs::make_page_identity(base + "/crawl/alias", "Child");
    ASSERT_TRUE(expected && alias);
    EXPECT_EQ(pages[0].page_id, expected->page_id);
    EXPECT_NE(pages[0].page_id, alias->page_id);
    EXPECT_EQ(pages[0].text_hash, "sha256:8053321281f5d162ee6c1c1263a0d1a88ee2d500e4c30c65dc343f37240aaefd");
    EXPECT_EQ(pages[1].text_hash, "sha256:fea5e82e5aadd9c7d464a66b92f331ce36d0947901fd6e9eb90234275418e26b");
    const auto previous_id = pages[0].page_id;
    const auto previous_hash = pages[0].text_hash;
    pages.clear();
    (void)run({base + "/crawl/alias-two"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(pages[0].page_id, previous_id);
    EXPECT_EQ(pages[0].text_hash, previous_hash);
}

TEST_F(Crawl, CapturesOneWallClockTimePerFinalResponseBeforeDelivery)
{
    using namespace std::chrono;
    const auto first = sys_days{2026y/November/9} + 12h;
    system_clock::time_point now = first;
    std::size_t calls{};
    options.wall_now = [&] {
        ++calls;
        const auto captured = now;
        now += 1s;
        return captured;
    };
    (void)run({base + "/crawl/alias"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(calls, 2U); // The intermediate redirect does not become a page record.
    ASSERT_TRUE(pages[0].fetched_at);
    ASSERT_TRUE(pages[1].fetched_at);
    EXPECT_EQ(*pages[0].fetched_at, first);
    EXPECT_EQ(*pages[1].fetched_at, first + 1s);
    now += 1h;
    EXPECT_EQ(*pages[0].fetched_at, first); // Stored instants do not follow later clock changes.
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

TEST_F(Crawl, SkipsUnsupportedOrBrokenHtmlAndContinuesWithValidUtf8)
{
    const auto summary = run({base + "/encoding/legacy", base + "/encoding/meta",
                              base + "/encoding/broken", base + "/encoding/ambiguous",
                              base + "/encoding/utf8", base + "/encoding/undeclared",
                              base + "/encoding/bom"});
    ASSERT_EQ(pages.size(), 3U);
    EXPECT_EQ(pages[0].title, "Caf\xC3\xA9");
    EXPECT_EQ(pages[1].title, "Undeclared");
    EXPECT_EQ(pages[2].title, "BOM");
    EXPECT_EQ(pages[2].text, "Clean body");
    EXPECT_EQ(summary.skipped_responses, 4U);
    EXPECT_EQ(summary.fetch_failures, 0U);
    EXPECT_EQ(summary.targets_processed, 7U);
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
    options.wall_now = {};
    EXPECT_THROW(run({base + "/must-not-follow"}), std::invalid_argument);
    options.wall_now = hfs::CrawlOptions::WallClock::now;

    const std::vector<std::string> seeds{base + "/crawl/b"};
    EXPECT_THROW((void)hfs::crawl(seeds, {"127.0.0.1"}, [](const auto&) {
        throw std::runtime_error{"Consumer failed"};
    }, options), std::runtime_error);
}
