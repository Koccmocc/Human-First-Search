#include "hfs/crawl.hpp"
#include "hfs/html_document.hpp"
#include "hfs/html_input.hpp"
#include "hfs/page_identity.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace hfs {

auto crawl(std::span<const std::string> seeds, std::vector<std::string> allowed_hosts,
           const PageConsumer& consume, const CrawlOptions& options) -> CrawlSummary
{
    if (!consume || !options.wall_now || options.max_pages == 0 || options.max_targets == 0) {
        throw std::invalid_argument{"Crawl requires a consumer, wall clock and positive page/target limits"};
    }

    UrlFrontier frontier{allowed_hosts, options.frontier};
    PageFetcher fetcher{std::move(allowed_hosts), options.robots, options.timing};
    CrawlSummary summary;
    for (const auto& seed : seeds) {
        if (frontier.add_seed(seed) != EnqueueResult::added) {
            ++summary.rejected_seeds;
        }
    }

    std::unordered_set<std::string> processed_urls;
    std::unordered_set<std::string> seen_text_hashes;
    const auto notify = [&options](const CrawlEvent& event) {
        if (options.on_event) {
            options.on_event(event);
        }
    };
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
        CrawlEvent event;
        event.target = *target;
        // A previous redirect may already have processed this queued destination.
        if (processed_urls.contains(target->url)) {
            ++summary.duplicate_pages;
            event.outcome = CrawlOutcome::duplicate_url;
            event.final_url = target->url;
            notify(event);
            continue;
        }

        auto result = fetcher.fetch(target->url, options.http, options.max_redirects);
        auto* fetched = std::get_if<FetchResponse>(&result);
        if (fetched == nullptr) {
            ++summary.fetch_failures;
            event.outcome = CrawlOutcome::fetch_failed;
            if (const auto* error = std::get_if<HttpError>(&result)) {
                event.fetch_error = *error;
            } else {
                event.fetch_error = std::get<FetchError>(result);
            }
            notify(event);
            continue;
        }
        // Capture completion before HTML parsing, hashing or consumer work begins.
        const auto fetched_at = options.wall_now();
        const auto& response = fetched->response;
        event.final_url = fetched->final_url;
        event.http_status = response.status;
        if (response.status == 200) {
            event.html_input = check_html_input(response.content_type, response.body);
        }
        // Error pages and other media must not become HTML discovery sources.
        if (response.status != 200 || event.html_input != HtmlInputStatus::ready) {
            ++summary.skipped_responses;
            event.outcome = CrawlOutcome::response_skipped;
            notify(event);
            continue;
        }
        if (!processed_urls.insert(fetched->final_url).second) {
            ++summary.duplicate_pages;
            event.outcome = CrawlOutcome::duplicate_url;
            notify(event);
            continue;
        }

        std::string_view html{response.body};
        if (html.starts_with("\xEF\xBB\xBF")) {
            html.remove_prefix(3); // The encoding signature is not document text.
        }
        const HtmlDocument document{html};
        const CrawlTarget parent{fetched->final_url, target->depth};
        auto extracted = document.main_text();
        if (extracted.text.empty()) {
            // Extraction has already normalized whitespace and removed ignored subtrees.
            ++summary.empty_texts;
            event.outcome = CrawlOutcome::empty_text;
        } else {
            const auto identity = make_page_identity(fetched->final_url, extracted.text);
            if (!identity) {
                throw std::runtime_error{"Fetched page has no valid canonical URL for its ID"};
            }
            if (seen_text_hashes.insert(identity->text_hash).second) {
                // FIFO chooses the first representative; only unique text uses the page budget.
                const CrawledPage page{fetched->final_url, document.title(), std::move(extracted.text),
                                       target->depth, response.status, response.content_type,
                                       std::move(fetched->requested_urls), identity->page_id, identity->text_hash,
                                       fetched_at, extracted.source};
                consume(page);
                ++summary.pages;
                event.outcome = CrawlOutcome::page_delivered;
            } else {
                ++summary.duplicate_texts;
                event.outcome = CrawlOutcome::duplicate_text;
            }
        }
        // Empty or duplicate text can still lead to useful pages.
        // Relative references belong to the final URL, not the redirect source.
        for (const auto& link : document.links()) {
            (void)frontier.add_link(parent, link);
        }
        // Report completion only after the consumer and link discovery succeeded.
        notify(event);
    }
    summary.pending_urls = frontier.size();
    return summary;
}

} // namespace hfs
