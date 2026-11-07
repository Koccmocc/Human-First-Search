#pragma once

#include "hfs/robots_loading.hpp"

#include <chrono>
#include <functional>
#include <unordered_map>

namespace hfs {

struct RobotsCacheOptions {
    std::chrono::seconds ttl{std::chrono::hours{24}};
    std::chrono::seconds retry_after{std::chrono::minutes{1}};
    HttpOptions http;
};

// An in-memory cache for a single-threaded crawl with a fixed bootstrap policy.
class RobotsCache {
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;

    explicit RobotsCache(RequestPermission permitted, RobotsCacheOptions options = {},
                         Now now = Clock::now);

    // Return an owning copy so changing the result leaves the cached entry intact.
    [[nodiscard]] auto get(std::string_view page_url) -> RobotsDocument;
    void clear();

private:
    struct Entry {
        RobotsDocument document;
        Clock::time_point loaded_at;
    };

    RequestPermission permitted_;
    RobotsCacheOptions options_;
    Now now_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace hfs
