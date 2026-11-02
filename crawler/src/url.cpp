#include "hfs/url.hpp"

#include <lexbor/url/url.h>

#include <exception>
#include <stdexcept>
#include <utility>

namespace hfs {
namespace {

struct Parser {
    lxb_url_parser_t value{};

    ~Parser()
    {
        // Lexbor separates parser buffers from the arena owning parsed URLs.
        lxb_url_parser_memory_destroy(&value);
        lxb_url_parser_destroy(&value, false);
    }
};

auto permitted(const lxb_url_t* url) -> bool
{
    return url != nullptr &&
        (url->scheme.type == LXB_URL_SCHEMEL_TYPE_HTTP ||
         url->scheme.type == LXB_URL_SCHEMEL_TYPE_HTTPS) &&
        url->username.length == 0 && url->password.length == 0;
}

struct SerializedUrl {
    std::string text;
    std::exception_ptr error;
};

auto append(const lxb_char_t* data, std::size_t length, void* context) noexcept -> lxb_status_t
{
    auto& output = *static_cast<SerializedUrl*>(context);
    try {
        if (length != 0) {
            output.text.append(reinterpret_cast<const char*>(data), length);
        }
        return LXB_STATUS_OK;
    } catch (...) {
        // Do not unwind a C++ exception through the C serializer's stack frames.
        output.error = std::current_exception();
        return LXB_STATUS_ERROR;
    }
}

} // namespace

auto resolve_http_url(std::string_view base, std::string_view reference)
    -> std::optional<std::string>
{
    Parser parser;
    if (lxb_url_parser_init(&parser.value, nullptr) != LXB_STATUS_OK) {
        throw std::runtime_error{"Could not initialize URL parser"};
    }
    const auto bytes = [](std::string_view value) {
        return reinterpret_cast<const lxb_char_t*>(value.empty() ? "" : value.data());
    };
    auto* base_url = lxb_url_parse(&parser.value, nullptr, bytes(base), base.size());
    if (!permitted(base_url)) {
        return std::nullopt;
    }

    // Cleaning parser state keeps the base URL alive in the shared arena.
    lxb_url_parser_clean(&parser.value);
    auto* url = lxb_url_parse(&parser.value, base_url, bytes(reference), reference.size());
    if (!permitted(url)) {
        return std::nullopt;
    }

    SerializedUrl output;
    const auto status = lxb_url_serialize(url, append, &output, true);
    if (output.error) {
        std::rethrow_exception(output.error);
    }
    if (status != LXB_STATUS_OK) {
        throw std::runtime_error{"Could not serialize URL"};
    }
    return std::move(output.text);
}

} // namespace hfs
