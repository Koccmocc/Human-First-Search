#include "hfs/page_fetcher.hpp"
#include "hfs/url.hpp"

#include <utility>

namespace hfs {

PageFetcher::PageFetcher(std::vector<std::string> allowed_hosts, RobotsCacheOptions robots_options)
    : in_scope_{[hosts = std::move(allowed_hosts)](std::string_view url) {
          // The closure owns the names; views exist only during this synchronous check.
          const std::vector<std::string_view> names{hosts.begin(), hosts.end()};
          return is_allowed_host(url, names);
      }},
      robots_{in_scope_, robots_options}
{
}

auto PageFetcher::fetch(std::string_view url, const HttpOptions& options,
                        std::size_t max_redirects) -> FetchResult
{
    return fetch_http(url, [this](std::string_view target) {
        // Filter scope before loading robots, including when a redirect changes origin.
        if (!in_scope_(target)) {
            return false;
        }
        return robots_.get(target).allows(target);
    }, options, max_redirects);
}

} // namespace hfs
