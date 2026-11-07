#pragma once

#include "hfs/robots_cache.hpp"
#include "hfs/host_delay.hpp"

#include <string>
#include <memory>
#include <vector>

namespace hfs {

// Owns host scope and robots state for a single-threaded crawl session.
class PageFetcher {
public:
    // Supply canonical host names, as returned by http_url_host().
    explicit PageFetcher(std::vector<std::string> allowed_hosts,
                         RobotsCacheOptions robots_options = {}, HostDelayOptions timing = {});

    [[nodiscard]] auto fetch(std::string_view url, const HttpOptions& options = {},
                             std::size_t max_redirects = 5) -> FetchResult;

private:
    std::shared_ptr<HostDelay> delay_;
    RequestPermission in_scope_;
    RobotsCache robots_;
};

} // namespace hfs
