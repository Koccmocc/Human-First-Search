#include "hfs/robots_loading.hpp"
#include "hfs/robots.hpp"
#include "hfs/url.hpp"

#include <utility>

namespace hfs {

auto RobotsDocument::allows(std::string_view page_url) const -> bool
{
    const auto location = robots_txt_url(page_url);
    if (!location || *location != source_url) {
        return false;
    }
    switch (state) {
    case RobotsState::rules:
        return robots_allows(text, page_url);
    case RobotsState::unavailable:
        return true;
    case RobotsState::unreachable:
        return false;
    }
    return false;
}

auto load_robots(std::string_view page_url, const RequestPermission& permitted,
                 const HttpOptions& options) -> RobotsDocument
{
    RobotsDocument document;
    const auto location = robots_txt_url(page_url);
    if (!location) {
        document.detail = "Invalid page URL for robots.txt retrieval";
        return document;
    }
    document.source_url = *location;
    if (options.max_body_bytes < 500 * 1024) {
        document.detail = "Robots body limit must support at least 500 KiB";
        return document;
    }

    auto result = fetch_http(*location, permitted, options);
    if (const auto* error = std::get_if<HttpError>(&result)) {
        document.detail = error->message;
        return document;
    }
    if (const auto* error = std::get_if<FetchError>(&result)) {
        document.detail = error->message;
        return document;
    }

    auto fetched = std::get<FetchResponse>(std::move(result));
    document.http_status = fetched.response.status;
    // Keep the initial origin as the rule owner even if robots.txt redirected.
    if (document.http_status == 200 || document.http_status == 204) {
        document.state = RobotsState::rules;
        document.text = std::move(fetched.response.body);
    } else if (document.http_status == 404 || document.http_status == 410) {
        document.state = RobotsState::unavailable;
    } else {
        document.detail = "Robots retrieval returned HTTP " + std::to_string(document.http_status);
    }
    return document;
}

} // namespace hfs
