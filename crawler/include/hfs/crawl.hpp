#pragma once

#include "hfs/page_fetcher.hpp"
#include "hfs/html_document.hpp"
#include "hfs/url_frontier.hpp"

#include <cstddef>
#include <chrono>
#include <optional>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace hfs {

struct CrawlOptions {
    using WallClock = std::chrono::system_clock;

    FrontierLimits frontier;
    HttpOptions http;
    RobotsCacheOptions robots;
    HostDelayOptions timing;
    std::size_t max_pages{1000}; // Records with nonempty, distinct extracted-text SHA-256 values.
    std::size_t max_targets{5000};
    std::size_t max_redirects{5};
    // Used only for fetch metadata; request spacing keeps its separate steady clock.
    std::function<WallClock::time_point()> wall_now{WallClock::now};
};

struct CrawledPage {
    std::string url;
    std::string title;
    std::string text;
    std::size_t depth{};
    long http_status{};
    std::string content_type;
    std::vector<std::string> requested_urls;
    // Stable URL identity and exact extracted-text fingerprint, both SHA-256.
    std::string page_id;
    std::string text_hash;
    // Observed after the final response returns, before validation/extraction.
    std::optional<std::chrono::system_clock::time_point> fetched_at;
    HtmlTextSource text_source{HtmlTextSource::body};
};

enum class CrawlStop { queue_empty, page_limit, target_limit };

struct CrawlSummary {
    CrawlStop stop{CrawlStop::queue_empty};
    std::size_t targets_processed{};
    std::size_t pages{};
    std::size_t fetch_failures{};
    std::size_t skipped_responses{};
    std::size_t empty_texts{};
    std::size_t duplicate_pages{};
    std::size_t duplicate_texts{};
    std::size_t rejected_seeds{};
    std::size_t pending_urls{};
};

// The page is borrowed only during the call. Copy it if it must outlive the callback.
using PageConsumer = std::function<void(const CrawledPage&)>;

// One synchronous session. Supply canonical host names. Only validated UTF-8 HTML
// reaches extraction; rejected metadata/encodings count as skipped_responses.
// Delivers the first page for each nonempty extracted-text SHA-256.
// Empty texts count as empty_texts; empty texts and duplicates still supply links.
// Setup, clock, allocation, parser and consumer exceptions abort the session and propagate.
[[nodiscard]] auto crawl(std::span<const std::string> seeds,
                         std::vector<std::string> allowed_hosts,
                         const PageConsumer& consume, const CrawlOptions& options = {})
    -> CrawlSummary;

} // namespace hfs
