#include "hfs/url_frontier.hpp"
#include "hfs/url.hpp"

#include <algorithm>
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
    if (pending_count_ == limits_.max_pending) {
        return EnqueueResult::queue_full;
    }

    const auto host = http_url_host(*url);
    if (!host) {
        return EnqueueResult::invalid_url;
    }
    const auto known = known_.insert(*url).first;
    try {
        const auto [level, created] = pending_.try_emplace(depth);
        try {
            auto& hosts_at_depth = level->second;
            const auto queue = std::find_if(hosts_at_depth.begin(), hosts_at_depth.end(),
                [&host](const auto& item) { return item.host == *host; });
            if (queue != hosts_at_depth.end()) {
                queue->targets.push_back(CrawlTarget{std::move(*url), depth});
            } else {
                // Construct a complete queue before publishing a new host into the ring.
                HostQueue added{*host, {}};
                added.targets.push_back(CrawlTarget{std::move(*url), depth});
                hosts_at_depth.push_back(std::move(added));
            }
        } catch (...) {
            if (created) {
                pending_.erase(level);
            }
            throw;
        }
    } catch (...) {
        // Failed allocation must not consume the URL budget or mark a candidate known.
        known_.erase(known);
        throw;
    }
    ++pending_count_;
    return EnqueueResult::added;
}

auto UrlFrontier::pop() -> std::optional<CrawlTarget>
{
    if (pending_.empty()) {
        return std::nullopt;
    }
    const auto level = pending_.begin(); // Every shallower pending target goes first.
    auto& hosts_at_depth = level->second;
    auto& targets = hosts_at_depth.front().targets;
    auto next = std::move(targets.front());
    targets.pop_front();
    --pending_count_;
    if (targets.empty()) {
        hosts_at_depth.pop_front();
        if (hosts_at_depth.empty()) {
            pending_.erase(level);
        }
    } else {
        hosts_at_depth.splice(hosts_at_depth.end(), hosts_at_depth, hosts_at_depth.begin());
    }
    return next;
}

auto UrlFrontier::size() const noexcept -> std::size_t
{
    return pending_count_;
}

auto UrlFrontier::accepted_count() const noexcept -> std::size_t
{
    return known_.size();
}

} // namespace hfs
