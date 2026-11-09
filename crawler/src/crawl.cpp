#include "hfs/crawl.hpp"
#include "hfs/html_document.hpp"
#include "hfs/html_input.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace hfs {

auto crawl(std::span<const std::string> seeds, std::vector<std::string> allowed_hosts,
           const PageConsumer& consume, const CrawlOptions& options) -> CrawlSummary
{
    if (!consume || options.max_pages == 0 || options.max_targets == 0) {
        throw std::invalid_argument{"Crawl requires a consumer and positive page/target limits"};
    }

    UrlFrontier frontier{allowed_hosts, options.frontier};
    PageFetcher fetcher{std::move(allowed_hosts), options.robots, options.timing};
    CrawlSummary summary;
    for (const auto& seed : seeds) {
        if (frontier.add_seed(seed) != EnqueueResult::added) {
            ++summary.rejected_seeds;
        }
    }

    std::unordered_set<std::string> emitted_urls;
    while (frontier.size() != 0) {
        if (summary.pages == options.max_pages) {
            summary.stop = CrawlStop::page_limit;
            break;
        }
        if (summary.targets_processed == options.max_targets) {
            summary.stop = CrawlStop::target_limit;
            break;
        }
        const auto target = frontier.pop();
        ++summary.targets_processed;
        // A previous redirect may already have delivered this queued destination.
        if (emitted_urls.contains(target->url)) {
            ++summary.duplicate_pages;
            continue;
        }

        auto result = fetcher.fetch(target->url, options.http, options.max_redirects);
        auto* fetched = std::get_if<FetchResponse>(&result);
        if (fetched == nullptr) {
            ++summary.fetch_failures;
            continue;
        }
        const auto& response = fetched->response;
        // Error pages and other media must not become HTML discovery sources.
        if (response.status != 200 ||
            check_html_input(response.content_type, response.body) != HtmlInputStatus::ready) {
            ++summary.skipped_responses;
            continue;
        }
        if (!emitted_urls.insert(fetched->final_url).second) {
            ++summary.duplicate_pages;
            continue;
        }

        std::string_view html{response.body};
        if (html.starts_with("\xEF\xBB\xBF")) {
            html.remove_prefix(3); // The encoding signature is not document text.
        }
        const HtmlDocument document{html};
        const CrawlTarget parent{fetched->final_url, target->depth};
        const CrawledPage page{fetched->final_url, document.title(), document.body_text(),
                               target->depth, response.status, response.content_type,
                               std::move(fetched->requested_urls)};
        consume(page);
        ++summary.pages;
        // Relative references belong to the final URL, not the redirect source.
        for (const auto& link : document.links()) {
            (void)frontier.add_link(parent, link);
        }
    }
    summary.pending_urls = frontier.size();
    return summary;
}

} // namespace hfs
