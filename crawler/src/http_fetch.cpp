#include "hfs/http_fetch.hpp"
#include "hfs/url.hpp"

#include <unordered_set>
#include <utility>

namespace hfs {

auto fetch_http(std::string_view url, const RequestPermission& permitted,
                const HttpOptions& options, std::size_t max_redirects) -> FetchResult
{
    auto current = resolve_http_url(url, "");
    if (!current) {
        return FetchError{FetchErrorKind::invalid_url, "Invalid initial HTTP(S) URL"};
    }

    std::unordered_set<std::string> visited;
    std::vector<std::string> requested;
    std::size_t redirects{};
    while (true) {
        // Detect a cycle before repeating either the policy callback or the request.
        if (!visited.insert(*current).second) {
            return FetchError{FetchErrorKind::redirect_loop, "Redirect cycle detected"};
        }
        if (!permitted || !permitted(*current)) {
            return FetchError{FetchErrorKind::request_denied, "Request denied by caller policy"};
        }
        requested.push_back(*current);
        auto result = http_get(*current, options);
        if (auto* error = std::get_if<HttpError>(&result)) {
            return std::move(*error);
        }
        auto response = std::get<HttpResponse>(std::move(result));
        if (!response.redirect_location) {
            return FetchResponse{std::move(*current), std::move(response), std::move(requested)};
        }
        if (redirects == max_redirects) {
            return FetchError{FetchErrorKind::redirect_limit, "Redirect limit reached"};
        }

        // Use the same URL rules for seed links and redirect destinations.
        current = resolve_http_url(*current, *response.redirect_location);
        if (!current) {
            return FetchError{FetchErrorKind::invalid_url, "Invalid or unsupported redirect target"};
        }
        ++redirects;
    }
}

} // namespace hfs
