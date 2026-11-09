#include "hfs/html_input.hpp"

#include <lexbor/encoding/encoding.h>
#include <lexbor/html/encoding.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

auto lower(std::string_view value) -> std::string
{
    std::string result{value};
    for (auto& ch : result) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
    }
    return result;
}

void skip_space(std::string_view& input)
{
    const auto first = input.find_first_not_of(" \t");
    input.remove_prefix(first == std::string_view::npos ? input.size() : first);
}

auto token(std::string_view& input) -> std::string_view
{
    std::size_t length{};
    for (const char ch : input) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || std::string_view{"!#$%&'*+-.^_`|~"}.find(ch)
                  != std::string_view::npos)) {
            break;
        }
        ++length;
    }
    const auto result = input.substr(0, length);
    input.remove_prefix(length);
    return result;
}

auto parameter_value(std::string_view& input) -> std::optional<std::string>
{
    if (!input.starts_with('"')) {
        const auto value = token(input);
        return value.empty() ? std::nullopt : std::optional{std::string{value}};
    }
    input.remove_prefix(1);
    std::string value;
    while (!input.empty()) {
        char ch = input.front();
        input.remove_prefix(1);
        if (ch == '"') {
            return value;
        }
        if (ch == '\\') {
            if (input.empty()) {
                return std::nullopt;
            }
            ch = input.front();
            input.remove_prefix(1);
        }
        const auto byte = static_cast<unsigned char>(ch);
        if ((byte < 0x20 && ch != '\t') || byte == 0x7F) {
            return std::nullopt;
        }
        value += ch;
    }
    return std::nullopt;
}

auto content_type(std::string_view input, std::optional<std::string>& charset)
    -> hfs::HtmlInputStatus
{
    using enum hfs::HtmlInputStatus;
    skip_space(input);
    const auto type = lower(token(input));
    if (!input.starts_with('/')) {
        return invalid_content_type;
    }
    input.remove_prefix(1);
    const auto subtype = lower(token(input));
    if (type != "text" || subtype != "html") {
        return not_html;
    }
    while (true) {
        skip_space(input);
        if (input.empty()) {
            return ready;
        }
        if (!input.starts_with(';')) {
            return invalid_content_type;
        }
        input.remove_prefix(1);
        skip_space(input);
        if (input.empty() || input.starts_with(';')) {
            continue; // HTTP parameters permit empty semicolon-delimited entries.
        }
        const auto name = lower(token(input));
        if (name.empty() || !input.starts_with('=')) {
            return invalid_content_type;
        }
        input.remove_prefix(1);
        auto value = parameter_value(input);
        if (!value) {
            return invalid_content_type;
        }
        if (name == "charset") {
            // Reject ambiguous duplicates rather than guessing which one wins.
            if (charset || value->empty()) {
                return invalid_content_type;
            }
            charset = std::move(*value);
        }
    }
}

auto encoding_label(std::string_view label) -> lxb_encoding_t
{
    const auto* data = lxb_encoding_data_by_pre_name(
        reinterpret_cast<const lxb_char_t*>(label.data()), label.size());
    return data == nullptr ? LXB_ENCODING_UNDEFINED : data->encoding;
}

auto meta_encoding(std::string_view body) -> lxb_encoding_t
{
    const auto destroy = [](lxb_html_encoding_t* value) { lxb_html_encoding_destroy(value, true); };
    const std::unique_ptr<lxb_html_encoding_t, decltype(destroy)> scanner{
        lxb_html_encoding_create(), destroy};
    if (!scanner || lxb_html_encoding_init(scanner.get()) != LXB_STATUS_OK) {
        throw std::runtime_error{"Could not initialize HTML encoding scanner"};
    }
    const auto* begin = reinterpret_cast<const lxb_char_t*>(body.empty() ? "" : body.data());
    if (lxb_html_encoding_determine(scanner.get(), begin, begin + std::min(body.size(), std::size_t{1024}))
        != LXB_STATUS_OK) {
        throw std::runtime_error{"Could not scan HTML encoding declarations"};
    }
    for (std::size_t i = 0; i < lxb_html_encoding_meta_length(scanner.get()); ++i) {
        const auto* entry = lxb_html_encoding_meta_entry(scanner.get(), i);
        const auto* data = lxb_encoding_data_prescan_validate(
            entry->name, static_cast<std::size_t>(entry->end - entry->name));
        if (data != nullptr) {
            return data->encoding;
        }
    }
    return LXB_ENCODING_UNDEFINED;
}

auto valid_text(std::string_view body) -> bool
{
    const auto* next = reinterpret_cast<const lxb_char_t*>(body.empty() ? "" : body.data());
    const auto* end = next + body.size();
    lxb_encoding_decode_t decoder{};
    while (next != end) {
        // This decoder validates continuation bytes, scalar range and truncation.
        const auto cp = lxb_encoding_decode_utf_8_single(&decoder, &next, end);
        if (cp == 0 || cp > LXB_ENCODING_MAX_CODEPOINT) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace hfs {

auto check_html_input(std::string_view type, std::string_view body) -> HtmlInputStatus
{
    std::optional<std::string> charset;
    const auto status = content_type(type, charset);
    if (status != HtmlInputStatus::ready) {
        return status;
    }

    // BOM wins over declarations. Unsupported Unicode BOMs must not reach the DOM.
    const auto* bytes = reinterpret_cast<const lxb_char_t*>(body.empty() ? "" : body.data());
    auto encoding = lxb_encoding_bom_sniff(bytes, body.size());
    if (body.starts_with(std::string_view{"\0\0\xFE\xFF", 4})) {
        return HtmlInputStatus::unsupported_encoding; // UTF-32BE has no Lexbor BOM entry.
    }
    if (encoding == LXB_ENCODING_DEFAULT) {
        encoding = charset ? encoding_label(*charset) : meta_encoding(body);
        if (charset && encoding == LXB_ENCODING_UNDEFINED) {
            return HtmlInputStatus::unsupported_encoding;
        }
    }
    if (encoding != LXB_ENCODING_UNDEFINED && encoding != LXB_ENCODING_UTF_8) {
        return HtmlInputStatus::unsupported_encoding;
    }
    // No declaration is our explicit UTF-8 fallback; always validate the bytes.
    return valid_text(body) ? HtmlInputStatus::ready : HtmlInputStatus::invalid_text;
}

} // namespace hfs
