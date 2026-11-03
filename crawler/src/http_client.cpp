#include "hfs/http_client.hpp"
#include "hfs/url.hpp"

#include <curl/curl.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace hfs {
namespace {

class CurlRuntime {
public:
    CurlRuntime()
    {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error{"Could not initialize libcurl"};
        }
    }
    ~CurlRuntime() { curl_global_cleanup(); }
};

struct BodyBuffer {
    std::string text;
    std::size_t limit;
    bool exceeded{};
    std::exception_ptr error;
};

auto receive(char* data, std::size_t size, std::size_t count, void* context) noexcept
    -> std::size_t
{
    auto& buffer = *static_cast<BodyBuffer*>(context);
    // Check before multiplying or appending, including responses without a length.
    const auto available = buffer.limit - buffer.text.size();
    if (size != 0 && count > available / size) {
        buffer.exceeded = true;
        return CURL_WRITEFUNC_ERROR;
    }
    const auto bytes = size * count;
    try {
        buffer.text.append(data, bytes);
        return bytes;
    } catch (...) {
        buffer.error = std::current_exception();
        return CURL_WRITEFUNC_ERROR;
    }
}

} // namespace

auto http_get(std::string_view url, const HttpOptions& options) -> HttpResult
{
    if (options.connect_timeout_ms <= 0 || options.timeout_ms <= 0 || options.max_body_bytes == 0) {
        return HttpError{HttpErrorKind::invalid_request, "Timeouts and body limit must be positive"};
    }
    const auto normalized = resolve_http_url(url, "");
    if (!normalized) {
        return HttpError{HttpErrorKind::invalid_request, "Expected an absolute HTTP(S) URL without credentials"};
    }

    // Function-local initialization happens once; each call still owns its handle.
    static const CurlRuntime runtime;
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{curl_easy_init(), &curl_easy_cleanup};
    if (!handle) {
        throw std::runtime_error{"Could not create libcurl handle"};
    }
    BodyBuffer buffer{{}, options.max_body_bytes, false, {}};
    const auto set = [&handle](CURLoption option, auto value) {
        if (curl_easy_setopt(handle.get(), option, value) != CURLE_OK) {
            throw std::runtime_error{"Could not configure HTTP request"};
        }
    };
    set(CURLOPT_URL, normalized->c_str());
    set(CURLOPT_PROTOCOLS_STR, "http,https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_CONNECTTIMEOUT_MS, options.connect_timeout_ms);
    set(CURLOPT_TIMEOUT_MS, options.timeout_ms);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_PROXY, "");
    set(CURLOPT_USERAGENT, "HumanFirstSearch/" HFS_VERSION);
    set(CURLOPT_WRITEFUNCTION, &receive);
    set(CURLOPT_WRITEDATA, &buffer);

    const auto code = curl_easy_perform(handle.get());
    if (buffer.error) {
        // Re-throw only after returning from C callbacks.
        std::rethrow_exception(buffer.error);
    }
    if (buffer.exceeded) {
        return HttpError{HttpErrorKind::body_limit, "Response body exceeds the configured limit"};
    }
    if (code != CURLE_OK) {
        return HttpError{code == CURLE_OPERATION_TIMEDOUT ? HttpErrorKind::timeout : HttpErrorKind::transport,
                         curl_easy_strerror(code)};
    }

    long status{};
    char* content_type{};
    if (curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK ||
        curl_easy_getinfo(handle.get(), CURLINFO_CONTENT_TYPE, &content_type) != CURLE_OK) {
        throw std::runtime_error{"Could not read HTTP response metadata"};
    }
    return HttpResponse{status, std::move(buffer.text), content_type != nullptr ? content_type : ""};
}

} // namespace hfs
