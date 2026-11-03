#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace hfs {

struct HttpOptions {
    long connect_timeout_ms{3000};
    long timeout_ms{10000};
    std::size_t max_body_bytes{2 * 1024 * 1024};
};

struct HttpResponse {
    long status{};
    std::string body;
    std::string content_type;
};

enum class HttpErrorKind { invalid_request, timeout, body_limit, transport };
struct HttpError {
    HttpErrorKind kind;
    std::string message;
};
using HttpResult = std::variant<HttpResponse, HttpError>;

// One synchronous HTTP(S) GET, without redirects or retries. Callers must check
// host scope and robots.txt before calling. HTTP 4xx/5xx remain normal responses.
[[nodiscard]] auto http_get(std::string_view url, const HttpOptions& options = {}) -> HttpResult;

} // namespace hfs
