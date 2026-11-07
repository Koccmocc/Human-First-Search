#pragma once

#include "hfs/robots_cache.hpp"

#include <string>
#include <vector>

namespace hfs {

// Owns host scope and robots state for a single-threaded crawl session.
class PageFetcher {
public:
    // Supply canonical host names, as returned by http_url_host().
    explicit PageFetcher(std::vector<std::string> allowed_hosts,
                         RobotsCacheOptions robots_options = {});

    [[nodiscard]] auto fetch(std::string_view url, const HttpOptions& options = {},
                             std::size_t max_redirects = 5) -> FetchResult;

private:
    RequestPermission in_scope_;
    RobotsCache robots_;
};

} // namespace hfs
