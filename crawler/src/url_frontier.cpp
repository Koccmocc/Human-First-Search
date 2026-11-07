#include "hfs/url_frontier.hpp"
#include "hfs/url.hpp"

#include <stdexcept>
#include <utility>

namespace hfs {

UrlFrontier::UrlFrontier(std::vector<std::string> allowed_hosts, FrontierLimits limits)
    : allowed_hosts_{std::move(allowed_hosts)}, limits_{limits}
{
    if (limits_.max_pending == 0 || limits_.max_urls == 0) {
        throw std::invalid_argument{"Frontier queue and URL limits must be positive"};
    }
}

auto UrlFrontier::add_seed(std::string_view url) -> EnqueueResult
{
    return enqueue(url, "", 0);
}

auto UrlFrontier::add_link(const CrawlTarget& parent, std::string_view reference) -> EnqueueResult
{
    // Check before incrementing, including when a caller supplies SIZE_MAX depth.
    if (parent.depth >= limits_.max_depth) {
        return EnqueueResult::depth_limit;
    }
    return enqueue(parent.url, reference, parent.depth + 1);
}

auto UrlFrontier::enqueue(std::string_view base, std::string_view reference,
                          std::size_t depth) -> EnqueueResult
{
    auto url = resolve_http_url(base, reference);
    if (!url) {
        return EnqueueResult::invalid_url;
    }
    const std::vector<std::string_view> hosts{allowed_hosts_.begin(), allowed_hosts_.end()};
    if (!is_allowed_host(*url, hosts)) {
        return EnqueueResult::out_of_scope;
    }
    if (known_.contains(*url)) {
        return EnqueueResult::duplicate;
    }
    if (known_.size() == limits_.max_urls) {
        return EnqueueResult::url_limit;
    }
    if (pending_.size() == limits_.max_pending) {
        return EnqueueResult::queue_full;
    }

    pending_.push_back(CrawlTarget{*url, depth});
    try {
        known_.insert(std::move(*url));
    } catch (...) {
        // Roll back admission if allocating deduplication storage fails.
        pending_.pop_back();
        throw;
    }
    return EnqueueResult::added;
}

auto UrlFrontier::pop() -> std::optional<CrawlTarget>
{
    if (pending_.empty()) {
        return std::nullopt;
    }
    auto next = std::move(pending_.front());
    pending_.pop_front();
    return next;
}

auto UrlFrontier::size() const noexcept -> std::size_t
{
    return pending_.size();
}

auto UrlFrontier::accepted_count() const noexcept -> std::size_t
{
    return known_.size();
}

} // namespace hfs
