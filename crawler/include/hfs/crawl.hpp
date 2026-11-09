#pragma once

#include "hfs/page_fetcher.hpp"
#include "hfs/url_frontier.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace hfs {

struct CrawlOptions {
    FrontierLimits frontier;
    HttpOptions http;
    RobotsCacheOptions robots;
    HostDelayOptions timing;
    std::size_t max_pages{1000};
    std::size_t max_targets{5000};
    std::size_t max_redirects{5};
};

struct CrawledPage {
    std::string url;
    std::string title;
    std::string text;
    std::size_t depth{};
    long http_status{};
    std::string content_type;
    std::vector<std::string> requested_urls;
};

enum class CrawlStop { queue_empty, page_limit, target_limit };

struct CrawlSummary {
    CrawlStop stop{CrawlStop::queue_empty};
    std::size_t targets_processed{};
    std::size_t pages{};
    std::size_t fetch_failures{};
    std::size_t skipped_responses{};
    std::size_t duplicate_pages{};
    std::size_t rejected_seeds{};
    std::size_t pending_urls{};
};

// The page is borrowed only during the call. Copy it if it must outlive the callback.
using PageConsumer = std::function<void(const CrawledPage&)>;

// One synchronous session. Supply canonical host names and UTF-8 HTML sources.
// Setup, allocation, parser and consumer exceptions abort the session and propagate.
[[nodiscard]] auto crawl(std::span<const std::string> seeds,
                         std::vector<std::string> allowed_hosts,
                         const PageConsumer& consume, const CrawlOptions& options = {})
    -> CrawlSummary;

} // namespace hfs
