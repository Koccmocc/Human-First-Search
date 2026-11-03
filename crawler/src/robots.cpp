#include "hfs/robots.hpp"
#include "hfs/crawler_identity.hpp"
#include "hfs/url.hpp"

#include <robots.h>

#include <string>

namespace hfs {

auto robots_allows(std::string_view robots_txt, std::string_view url) -> bool
{
    const auto normalized = resolve_http_url(url, "");
    if (!normalized) {
        return false;
    }

    // The matcher reparses the text for this check. Keep it local: upstream's
    // matcher is mutable and not thread-safe, and must not leak state across calls.
    googlebot::RobotsMatcher matcher;
    const auto text = robots_txt.empty() ? std::string_view{""} : robots_txt;
    return matcher.OneAgentAllowedByRobots(
        absl::string_view{text.data(), text.size()}, std::string{crawler_product}, *normalized);
}

} // namespace hfs
