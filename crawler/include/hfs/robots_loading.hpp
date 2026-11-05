#pragma once

#include "hfs/http_fetch.hpp"

namespace hfs {

enum class RobotsState { rules, unavailable, unreachable };

struct RobotsDocument {
    RobotsState state{RobotsState::unreachable};
    std::string source_url;
    std::string text;
    long http_status{};
    std::string detail;

    // Access is evaluated only for the origin associated with source_url.
    [[nodiscard]] auto allows(std::string_view page_url) const -> bool;
};

// Permission checks host scope for robots retrieval and each redirect destination.
// Supply this bootstrap policy independently of the rules being downloaded.
// Limits apply per request. The body limit must support at least 500 KiB.
[[nodiscard]] auto load_robots(std::string_view page_url, const RequestPermission& permitted,
                               const HttpOptions& options = {}) -> RobotsDocument;

} // namespace hfs
