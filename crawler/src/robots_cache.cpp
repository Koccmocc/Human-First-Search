#include "hfs/robots_cache.hpp"
#include "hfs/url.hpp"

#include <stdexcept>
#include <utility>

namespace hfs {

RobotsCache::RobotsCache(RequestPermission permitted, RobotsCacheOptions options, Now now)
    : permitted_{std::move(permitted)}, options_{options}, now_{std::move(now)}
{
    if (!now_ || options_.ttl <= std::chrono::seconds::zero() ||
        options_.ttl > std::chrono::hours{24} ||
        options_.retry_after <= std::chrono::seconds::zero() ||
        options_.retry_after > options_.ttl) {
        throw std::invalid_argument{"Expected a clock and 0 < retry_after <= ttl <= 24 hours"};
    }
}

auto RobotsCache::get(std::string_view page_url) -> RobotsDocument
{
    const auto key = robots_txt_url(page_url);
    if (!key) {
        RobotsDocument invalid;
        invalid.detail = "Invalid page URL for robots cache";
        return invalid;
    }

    const auto current = now_();
    const auto found = entries_.find(*key);
    if (found != entries_.end()) {
        const auto& entry = found->second;
        const auto lifetime = entry.document.state == RobotsState::unreachable
            ? options_.retry_after : options_.ttl;
        if (current >= entry.loaded_at && current - entry.loaded_at < lifetime) {
            return entry.document;
        }
    }

    // Fetch before replacing the entry. Exceptions leave the previous entry expired.
    auto document = load_robots(*key, permitted_, options_.http);
    const auto completed = now_();
    const auto entry = entries_.insert_or_assign(*key, Entry{std::move(document), completed}).first;
    return entry->second.document;
}

void RobotsCache::clear()
{
    entries_.clear();
}

} // namespace hfs
