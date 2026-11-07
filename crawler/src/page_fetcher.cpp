#include "hfs/page_fetcher.hpp"
#include "hfs/url.hpp"

#include <utility>

namespace hfs {

PageFetcher::PageFetcher(std::vector<std::string> allowed_hosts, RobotsCacheOptions robots_options,
                         HostDelayOptions timing)
    : delay_{std::make_shared<HostDelay>(std::move(timing))},
      in_scope_{[hosts = std::move(allowed_hosts)](std::string_view url) {
          // The closure owns the names; views exist only during this synchronous check.
          const std::vector<std::string_view> names{hosts.begin(), hosts.end()};
          return is_allowed_host(url, names);
      }},
      robots_{[scope = in_scope_, delay = delay_](std::string_view url) {
          if (!scope(url)) {
              return false;
          }
          // Robots and page requests share timing, including after a copy or move.
          delay->wait(url);
          return true;
      }, robots_options}
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
        if (!robots_.get(target).allows(target)) {
            return false;
        }
        delay_->wait(target);
        return true;
    }, options, max_redirects);
}

} // namespace hfs
