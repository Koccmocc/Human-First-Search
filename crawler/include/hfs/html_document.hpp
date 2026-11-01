#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace hfs {

// Owns a parsed DOM. The input must be UTF-8 and is borrowed only during parsing.
class HtmlDocument {
public:
    // Throws std::runtime_error on a Lexbor failure; malformed HTML is tolerated.
    explicit HtmlDocument(std::string_view html);
    ~HtmlDocument();

    HtmlDocument(const HtmlDocument&) = delete;
    auto operator=(const HtmlDocument&) -> HtmlDocument& = delete;
    HtmlDocument(HtmlDocument&&) noexcept;
    auto operator=(HtmlDocument&&) noexcept -> HtmlDocument&;

    // Returns an owning, whitespace-normalized title with HTML entities decoded.
    // Missing titles and moved-from documents produce an empty string.
    [[nodiscard]] auto title() const -> std::string;

private:
    // Hide Lexbor types so callers do not depend on its headers or allocation API.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hfs
