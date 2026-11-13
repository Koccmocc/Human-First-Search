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
        // Existing scenarios isolate mechanics using deliberately short fixture text.
        options.language_policy = hfs::CrawlLanguagePolicy::any;
    }

    auto run(std::vector<std::string> seeds) -> hfs::CrawlSummary
    {
        return hfs::crawl(seeds, {"127.0.0.1"}, collect, options);
    }
};

TEST_F(Crawl, EnglishAdmissionRejectsOtherAndUnknownTextButDiscoversItsLinks)
{
    options.language_policy = hfs::CrawlLanguagePolicy::english_only;
    std::vector<hfs::CrawlEvent> events;
    options.on_event = [&](const auto& event) { events.push_back(event); };
    const auto summary = run({base + "/language/german-alias", base + "/language/german",
                              base + "/language/short", base + "/language/numbers"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(pages[0].url, base + "/language/english");
    EXPECT_EQ(pages[0].text_source, hfs::HtmlTextSource::main_role);
    EXPECT_EQ(pages[0].text.find("Site controls"), std::string::npos);
    EXPECT_EQ(pages[1].url, base + "/language/english-two");
    for (const auto& page : pages) {
        EXPECT_EQ(page.depth, 1U);
        ASSERT_TRUE(page.language);
        EXPECT_EQ(page.language->code, "en");
        EXPECT_EQ(page.language->status, hfs::LanguageStatus::detected);
        const auto identity = hfs::make_page_identity(page.url, page.text);
        ASSERT_TRUE(identity);
        EXPECT_EQ(page.text_hash, identity->text_hash);
    }
    EXPECT_EQ(summary.pages, 2U);
    EXPECT_EQ(summary.non_english_pages, 1U);
    EXPECT_EQ(summary.unknown_language_pages, 2U);
    EXPECT_EQ(summary.duplicate_pages, 1U);
    EXPECT_EQ(summary.duplicate_texts, 0U);
    EXPECT_EQ(summary.targets_processed, 6U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
    ASSERT_EQ(events.size(), 6U);
    EXPECT_EQ(events[0].outcome, hfs::CrawlOutcome::non_english);
    ASSERT_TRUE(events[0].language);
    EXPECT_EQ(events[0].language->code, "de"); // HTML lang/title must not override main text.
    EXPECT_EQ(events[1].outcome, hfs::CrawlOutcome::duplicate_url);
    EXPECT_FALSE(events[1].language); // This destination was already processed.
    EXPECT_EQ(events[2].outcome, hfs::CrawlOutcome::unknown_language);
    ASSERT_TRUE(events[2].language);
    EXPECT_EQ(events[2].language->status, hfs::LanguageStatus::too_short);
    ASSERT_TRUE(events[3].language);
    EXPECT_EQ(events[3].language->status, hfs::LanguageStatus::unreliable);
}

TEST_F(Crawl, LanguageRejectionsUseTargetsButNotTheDeliveredPageBudget)
{
    options.language_policy = hfs::CrawlLanguagePolicy::english_only;
    options.max_pages = 1;
    auto summary = run({base + "/language/german", base + "/language/short"});
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0].url, base + "/language/english");
    EXPECT_EQ(summary.stop, hfs::CrawlStop::page_limit);
    EXPECT_EQ(summary.targets_processed, 3U);
    EXPECT_EQ(summary.non_english_pages, 1U);
    EXPECT_EQ(summary.unknown_language_pages, 1U);
    EXPECT_EQ(summary.pending_urls, 1U);

    pages.clear();
    options.max_targets = 1;
    summary = run({base + "/language/german"});
    EXPECT_TRUE(pages.empty());
    EXPECT_EQ(summary.stop, hfs::CrawlStop::target_limit);
    EXPECT_EQ(summary.targets_processed, 1U);
    EXPECT_EQ(summary.pending_urls, 1U);
}

TEST_F(Crawl, AnyLanguageModeRetainsDetectionAndAdmitsUnknownText)
{
    options.frontier.max_depth = 0;
    const auto summary = run({base + "/language/german", base + "/language/short"});
    ASSERT_EQ(pages.size(), 2U);
    ASSERT_TRUE(pages[0].language);
    EXPECT_EQ(pages[0].language->code, "de");
    ASSERT_TRUE(pages[1].language);
    EXPECT_EQ(pages[1].language->status, hfs::LanguageStatus::too_short);
    EXPECT_FALSE(pages[1].language->code);
    EXPECT_EQ(summary.non_english_pages, 0U);
    EXPECT_EQ(summary.unknown_language_pages, 0U);
}

TEST_F(Crawl, AdmittedEnglishTextStillDeduplicatesAndDuplicateLinksAreDiscovered)
{
    options.language_policy = hfs::CrawlLanguagePolicy::english_only;
    options.max_pages = 2;
    const auto summary = run({base + "/language/english", base + "/language/english-copy"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(pages[1].url, base + "/language/english-two");
    EXPECT_EQ(summary.duplicate_texts, 1U);
    EXPECT_EQ(summary.targets_processed, 3U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

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

TEST_F(Crawl, RoundRobinsTwoHostsAtEachDepthDespiteUnequalLinkCounts)
{
    std::string other = base;
    other.replace(other.find("127.0.0.1"), std::string{"127.0.0.1"}.size(), "localhost");
    options.max_pages = 6;
    options.frontier.max_depth = 1;
    std::vector<hfs::CrawlEvent> events;
    options.on_event = [&](const auto& event) { events.push_back(event); };
    const std::vector<std::string> seeds{base + "/balance/a-root", other + "/balance/b-root"};
    const auto summary = hfs::crawl(seeds, {"127.0.0.1", "localhost"}, collect, options);
    const std::vector<std::string> expected{
        base + "/balance/a-root", other + "/balance/b-root",
        base + "/balance/a/1", other + "/balance/b/1",
        base + "/balance/a/2", other + "/balance/b/2"};
    ASSERT_EQ(pages.size(), expected.size());
    ASSERT_EQ(events.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(pages[index].url, expected[index]);
        EXPECT_EQ(pages[index].depth, index < 2 ? 0U : 1U);
        EXPECT_EQ(events[index].target.url, expected[index]);
        EXPECT_EQ(events[index].outcome, hfs::CrawlOutcome::page_delivered);
    }
    EXPECT_EQ(summary.stop, hfs::CrawlStop::page_limit);
    EXPECT_EQ(summary.pages, 6U);
    EXPECT_EQ(summary.targets_processed, 6U);
    EXPECT_EQ(summary.fetch_failures, 0U);
    EXPECT_EQ(summary.pending_urls, 11U); // 12 + 3 discovered children, four processed.
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

TEST_F(Crawl, SkipsDuplicateTextButDiscoversItsLinksAndRemembersItsFinalUrl)
{
    const auto summary = run({base + "/dedup/z", base + "/dedup/a-alias", base + "/dedup/a"});
    ASSERT_EQ(pages.size(), 3U);
    EXPECT_EQ(pages[0].url, base + "/dedup/z");
    EXPECT_EQ(pages[0].title, "First title");
    EXPECT_EQ(pages[0].text, "Same & body\nNext");
    EXPECT_EQ(pages[1].url, base + "/dedup/from-z");
    EXPECT_EQ(pages[2].url, base + "/dedup/from-a");
    EXPECT_EQ(pages[2].depth, 1U);
    EXPECT_EQ(summary.pages, 3U);
    EXPECT_EQ(summary.targets_processed, 5U);
    EXPECT_EQ(summary.duplicate_texts, 1U);
    EXPECT_EQ(summary.duplicate_pages, 1U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

TEST_F(Crawl, ChoosesTheFirstTextRepresentativeWithinEachIndependentSession)
{
    options.frontier.max_depth = 0;
    (void)run({base + "/dedup/z", base + "/dedup/a"});
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0].url, base + "/dedup/z");
    const auto id = pages[0].page_id;
    const auto hash = pages[0].text_hash;
    pages.clear();
    (void)run({base + "/dedup/z", base + "/dedup/a"});
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0].page_id, id);
    EXPECT_EQ(pages[0].text_hash, hash);
    pages.clear();
    (void)run({base + "/dedup/a", base + "/dedup/z"});
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0].url, base + "/dedup/a");
    EXPECT_NE(pages[0].page_id, id);
    EXPECT_EQ(pages[0].text_hash, hash);
}

TEST_F(Crawl, CountsTextDuplicatesAgainstTargetsButNotTheUniquePageLimit)
{
    options.max_pages = 2;
    auto summary = run({base + "/dedup/z", base + "/dedup/a"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(pages[1].url, base + "/dedup/from-z");
    EXPECT_EQ(summary.targets_processed, 3U);
    EXPECT_EQ(summary.duplicate_texts, 1U);
    EXPECT_EQ(summary.pending_urls, 1U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::page_limit);

    pages.clear();
    options.max_pages = 10;
    options.max_targets = 2;
    summary = run({base + "/dedup/z", base + "/dedup/a"});
    EXPECT_EQ(pages.size(), 1U);
    EXPECT_EQ(summary.targets_processed, 2U);
    EXPECT_EQ(summary.duplicate_texts, 1U);
    EXPECT_EQ(summary.pending_urls, 2U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::target_limit);
}

TEST_F(Crawl, KeepsDifferentTextAndRejectsEachEmptyBody)
{
    options.frontier.max_depth = 0;
    auto summary = run({base + "/dedup/z", base + "/dedup/case"});
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_NE(pages[0].text_hash, pages[1].text_hash);
    EXPECT_EQ(summary.duplicate_texts, 0U);
    pages.clear();
    summary = run({base + "/dedup/empty-one", base + "/dedup/empty-two"});
    EXPECT_TRUE(pages.empty());
    EXPECT_EQ(summary.pages, 0U);
    EXPECT_EQ(summary.empty_texts, 2U);
    EXPECT_EQ(summary.duplicate_texts, 0U);
    EXPECT_EQ(summary.skipped_responses, 0U);
    EXPECT_EQ(summary.targets_processed, 2U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

TEST_F(Crawl, DiscoversLinksFromEmptyTextWithoutUsingThePageBudget)
{
    options.max_pages = 1;
    const auto summary = run({base + "/content/empty"});
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0].url, base + "/content/outside");
    EXPECT_EQ(pages[0].text, "Other page");
    EXPECT_EQ(pages[0].depth, 1U);
    EXPECT_EQ(summary.pages, 1U);
    EXPECT_EQ(summary.targets_processed, 2U);
    EXPECT_EQ(summary.empty_texts, 1U);
    EXPECT_EQ(summary.duplicate_texts, 0U);
    EXPECT_EQ(summary.pending_urls, 0U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

TEST_F(Crawl, EmptyTextStillUsesTheTargetBudgetAndDiscoveryDepthLimit)
{
    options.max_targets = 1;
    auto summary = run({base + "/content/empty"});
    EXPECT_TRUE(pages.empty());
    EXPECT_EQ(summary.pages, 0U);
    EXPECT_EQ(summary.empty_texts, 1U);
    EXPECT_EQ(summary.targets_processed, 1U);
    EXPECT_EQ(summary.pending_urls, 1U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::target_limit);

    options.max_targets = 10;
    options.frontier.max_depth = 0;
    summary = run({base + "/content/empty"});
    EXPECT_TRUE(pages.empty());
    EXPECT_EQ(summary.empty_texts, 1U);
    EXPECT_EQ(summary.pending_urls, 0U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::queue_empty);
}

TEST_F(Crawl, HashesSelectedContentAndDiscoversLinksOutsideItsContainer)
{
    const auto summary = run({base + "/content/page", base + "/content/copy"});
    ASSERT_EQ(pages.size(), 3U);
    EXPECT_EQ(pages[0].url, base + "/content/page");
    EXPECT_EQ(pages[0].text_source, hfs::HtmlTextSource::article);
    EXPECT_EQ(pages[0].text, "Heading\nCaf\xC3\xA9 & tea");
    const auto expected = hfs::make_page_identity(pages[0].url, pages[0].text);
    ASSERT_TRUE(expected);
    EXPECT_EQ(pages[0].text_hash, expected->text_hash);
    EXPECT_EQ(pages[1].url, base + "/content/outside");
    EXPECT_EQ(pages[1].text_source, hfs::HtmlTextSource::main);
    EXPECT_EQ(pages[2].url, base + "/content/copy-child");
    EXPECT_EQ(pages[2].text_source, hfs::HtmlTextSource::body);
    EXPECT_EQ(summary.duplicate_texts, 1U);
    EXPECT_EQ(summary.targets_processed, 4U);
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
    ASSERT_EQ(pages.size(), 2U);
    EXPECT_EQ(pages[0].title, "Caf\xC3\xA9");
    // The undeclared page has the same body text as the first UTF-8 page.
    EXPECT_EQ(pages[1].title, "BOM");
    EXPECT_EQ(pages[1].text, "Clean body");
    EXPECT_EQ(summary.duplicate_texts, 1U);
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

TEST_F(Crawl, ReportsOwnedEventsWithTypedReasonsForEachCompletedTarget)
{
    options.frontier.max_depth = 0;
    std::vector<hfs::CrawlEvent> events;
    options.on_event = [&](const auto& event) { events.push_back(event); };
    const std::vector<std::string> seeds{
        base + "/crawl/alias", base + "/crawl/nested/page", base + "/crawl/alias-two",
        base + "/dedup/z", base + "/dedup/a", base + "/content/empty",
        base + "/crawl/error", base + "/encoding/broken", base + "/private/page",
        base + "/partial", base + "/loop-a"
    };
    const auto summary = run(seeds);
    using enum hfs::CrawlOutcome;
    const std::vector<hfs::CrawlOutcome> expected{
        page_delivered, duplicate_url, duplicate_url, page_delivered, duplicate_text,
        empty_text, response_skipped, response_skipped, fetch_failed, fetch_failed, fetch_failed
    };
    ASSERT_EQ(events.size(), seeds.size());
    EXPECT_EQ(events.size(), summary.targets_processed);
    for (std::size_t index = 0; index < events.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_EQ(events[index].target.url, seeds[index]);
        EXPECT_EQ(events[index].target.depth, 0U);
        EXPECT_EQ(events[index].outcome, expected[index]);
    }
    EXPECT_EQ(summary.pages, 2U);
    EXPECT_EQ(summary.duplicate_pages, 2U);
    EXPECT_EQ(summary.duplicate_texts, 1U);
    EXPECT_EQ(summary.empty_texts, 1U);
    EXPECT_EQ(summary.skipped_responses, 2U);
    EXPECT_EQ(summary.fetch_failures, 3U);

    EXPECT_EQ(events[0].final_url, base + "/crawl/nested/page");
    EXPECT_EQ(events[0].http_status, 200);
    EXPECT_EQ(events[0].html_input, hfs::HtmlInputStatus::ready);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(events[0].fetch_error));
    EXPECT_EQ(events[1].final_url, events[0].final_url);
    EXPECT_FALSE(events[1].http_status); // This queued destination needed no request.
    EXPECT_FALSE(events[1].html_input);
    EXPECT_EQ(events[2].final_url, events[0].final_url);
    EXPECT_EQ(events[2].http_status, 200); // Another alias required a fetch first.
    EXPECT_EQ(events[6].http_status, 404);
    EXPECT_FALSE(events[6].html_input);
    EXPECT_EQ(events[7].html_input, hfs::HtmlInputStatus::invalid_text);
    for (const auto index : {8U, 9U, 10U}) {
        EXPECT_FALSE(events[index].final_url);
        EXPECT_FALSE(events[index].http_status);
        EXPECT_FALSE(events[index].html_input);
    }
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(events[8].fetch_error));
    EXPECT_EQ(std::get<hfs::FetchError>(events[8].fetch_error).kind,
              hfs::FetchErrorKind::request_denied);
    ASSERT_TRUE(std::holds_alternative<hfs::HttpError>(events[9].fetch_error));
    EXPECT_EQ(std::get<hfs::HttpError>(events[9].fetch_error).kind, hfs::HttpErrorKind::transport);
    ASSERT_TRUE(std::holds_alternative<hfs::FetchError>(events[10].fetch_error));
    EXPECT_EQ(std::get<hfs::FetchError>(events[10].fetch_error).kind,
              hfs::FetchErrorKind::redirect_loop);
}

TEST_F(Crawl, EventsPreserveDiscoveryAndExcludeRejectedSeedsAndPendingTargets)
{
    options.max_pages = 1;
    std::vector<hfs::CrawlEvent> events;
    options.on_event = [&](const auto& event) { events.push_back(event); };
    auto summary = run({base + "/content/empty"});
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].outcome, hfs::CrawlOutcome::empty_text);
    EXPECT_EQ(events[1].outcome, hfs::CrawlOutcome::page_delivered);
    EXPECT_EQ(events[1].target.url, base + "/content/outside");
    EXPECT_EQ(events[1].target.depth, 1U);
    EXPECT_EQ(summary.pages, 1U);

    events.clear();
    pages.clear();
    summary = run({"relative", base + "/crawl/start"});
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].target.url, base + "/crawl/start");
    EXPECT_EQ(summary.rejected_seeds, 1U);
    EXPECT_EQ(summary.pending_urls, 3U);
    EXPECT_EQ(summary.stop, hfs::CrawlStop::page_limit);
}

TEST_F(Crawl, ObserverRunsAfterConsumerAndItsExceptionsAbortTheSession)
{
    std::size_t calls{};
    options.on_event = [&](const auto&) {
        ++calls;
        EXPECT_EQ(pages.size(), 1U); // Delivery must have completed before the event.
        throw std::runtime_error{"Observer failed"};
    };
    EXPECT_THROW(run({base + "/crawl/start"}), std::runtime_error);
    EXPECT_EQ(calls, 1U);

    calls = 0;
    const std::vector<std::string> seeds{base + "/crawl/b"};
    EXPECT_THROW((void)hfs::crawl(seeds, {"127.0.0.1"}, [](const auto&) {
        throw std::runtime_error{"Consumer failed"};
    }, options), std::runtime_error);
    EXPECT_EQ(calls, 0U); // An aborted target is not reported as completed.
}

TEST_F(Crawl, ExpiredDurationLeavesTheFirstTargetPendingWithoutFetching)
{
    using namespace std::chrono;
    options.max_duration = 10ms;
    std::size_t ticks{};
    options.steady_now = [&] {
        return hfs::CrawlOptions::SteadyClock::time_point{} + (ticks++ == 0 ? 0ms : 10ms);
    };
    std::size_t events{};
    options.on_event = [&](const auto&) { ++events; };
    const auto summary = run({base + "/must-not-follow"});
    EXPECT_EQ(summary.stop, hfs::CrawlStop::duration_limit);
    EXPECT_EQ(summary.targets_processed, 0U);
    EXPECT_EQ(summary.pending_urls, 1U);
    EXPECT_EQ(summary.pages, 0U);
    EXPECT_EQ(events, 0U);
    EXPECT_TRUE(pages.empty());
}

TEST_F(Crawl, DurationStopsAfterCompletedDeliveryDiscoveryAndEvent)
{
    using namespace std::chrono;
    auto now = hfs::CrawlOptions::SteadyClock::time_point{};
    options.max_duration = 10ms;
    options.steady_now = [&] { return now; };
    std::vector<hfs::CrawlEvent> events;
    options.on_event = [&](const auto& event) {
        events.push_back(event);
        now += 10ms; // Include synchronous observer work in the next budget check.
    };
    const auto summary = run({base + "/crawl/start"});
    EXPECT_EQ(summary.stop, hfs::CrawlStop::duration_limit);
    EXPECT_EQ(summary.pages, 1U);
    EXPECT_EQ(summary.targets_processed, 1U);
    EXPECT_EQ(summary.pending_urls, 3U);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].outcome, hfs::CrawlOutcome::page_delivered);
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0].url, base + "/crawl/start");
}

TEST_F(Crawl, DurationAlsoAccountsForFailedAndEmptyTargets)
{
    using namespace std::chrono;
    auto now = hfs::CrawlOptions::SteadyClock::time_point{};
    options.max_duration = 10ms;
    options.steady_now = [&] { return now; };
    options.on_event = [&](const auto&) { now += 5ms; };
    const auto summary = run({base + "/private/page", base + "/content/empty",
                              base + "/must-not-follow"});
    EXPECT_EQ(summary.stop, hfs::CrawlStop::duration_limit);
    EXPECT_EQ(summary.targets_processed, 2U);
    EXPECT_EQ(summary.fetch_failures, 1U);
    EXPECT_EQ(summary.empty_texts, 1U);
    EXPECT_EQ(summary.pages, 0U);
    EXPECT_EQ(summary.pending_urls, 2U); // Includes the empty page's discovered child.
}

TEST_F(Crawl, QueuePageAndTargetStopsTakePrecedenceOverExpiredDuration)
{
    using namespace std::chrono;
    auto now = hfs::CrawlOptions::SteadyClock::time_point{};
    options.max_duration = 10ms;
    options.steady_now = [&] { return now; };
    options.on_event = [&](const auto&) { now += 10ms; };
    EXPECT_EQ(run({base + "/crawl/b"}).stop, hfs::CrawlStop::queue_empty);
    options.max_pages = 1;
    EXPECT_EQ(run({base + "/crawl/start"}).stop, hfs::CrawlStop::page_limit);
    options.max_pages = 10;
    options.max_targets = 1;
    EXPECT_EQ(run({base + "/crawl/start"}).stop, hfs::CrawlStop::target_limit);
}

TEST_F(Crawl, ValidatesDurationAndClockAndHandlesLargeBudgetsWithoutOverflow)
{
    using namespace std::chrono;
    for (const auto duration : {0ms, -1ms}) {
        options.max_duration = duration;
        EXPECT_THROW(run({base + "/must-not-follow"}), std::invalid_argument);
    }
    options.max_duration = 1s;
    options.steady_now = {};
    EXPECT_THROW(run({base + "/must-not-follow"}), std::invalid_argument);
    options.steady_now = []() -> hfs::CrawlOptions::SteadyClock::time_point {
        throw std::logic_error{"Clock failed"};
    };
    EXPECT_THROW(run({base + "/must-not-follow"}), std::logic_error);

    std::size_t ticks{};
    options.steady_now = [&] { return hfs::CrawlOptions::SteadyClock::time_point{} + milliseconds{ticks++}; };
    options.max_duration = milliseconds::max();
    EXPECT_EQ(run({base + "/crawl/b"}).pages, 1U);
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
    options.language_policy = static_cast<hfs::CrawlLanguagePolicy>(-1);
    EXPECT_THROW(run({base + "/must-not-follow"}), std::invalid_argument);
    options.language_policy = hfs::CrawlLanguagePolicy::any;

    const std::vector<std::string> seeds{base + "/crawl/b"};
    EXPECT_THROW((void)hfs::crawl(seeds, {"127.0.0.1"}, [](const auto&) {
        throw std::runtime_error{"Consumer failed"};
    }, options), std::runtime_error);
}
