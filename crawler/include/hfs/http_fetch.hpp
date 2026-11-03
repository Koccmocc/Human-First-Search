#pragma once

#include "hfs/http_client.hpp"

#include <functional>
#include <vector>

namespace hfs {

struct FetchResponse {
    std::string final_url;
    HttpResponse response;
    std::vector<std::string> requested_urls;
};

enum class FetchErrorKind { invalid_url, request_denied, redirect_loop, redirect_limit };
struct FetchError {
    FetchErrorKind kind;
    std::string message;
};
using FetchResult = std::variant<FetchResponse, HttpError, FetchError>;
using RequestPermission = std::function<bool(std::string_view)>;

// Permission is mandatory for every request, including the initial URL. An empty
// callback denies all. Callers supply scope/robots policy; exceptions propagate.
// max_redirects counts transitions, so zero still permits the initial request.
[[nodiscard]] auto fetch_http(std::string_view url, const RequestPermission& permitted,
                              const HttpOptions& options = {}, std::size_t max_redirects = 5)
    -> FetchResult;

} // namespace hfs
