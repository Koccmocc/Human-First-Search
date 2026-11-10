#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hfs {

inline constexpr std::string_view html_extractor_version{"semantic-text-v1"};

enum class HtmlTextSource { main, article, body };

struct HtmlText {
    std::string text;
    HtmlTextSource source{HtmlTextSource::body};
};

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

    // Extracts body text, not main-article text or CSS-computed visible text.
    // Block boundaries become newlines; inline markup adds no extra spaces.
    [[nodiscard]] auto body_text() const -> std::string;

    // First nonempty HTML main, then article, otherwise body. Ignores nav/aside/
    // footer and hidden subtrees in addition to script/style/template. This is a
    // semantic-container heuristic, without CSS evaluation or content scoring.
    [[nodiscard]] auto main_text() const -> HtmlText;

    // Returns decoded href values from HTML a/area elements in document order.
    // Keeps empty values and duplicates; URL resolution/filtering belongs elsewhere.
    [[nodiscard]] auto links() const -> std::vector<std::string>;

private:
    // Hide Lexbor types so callers do not depend on its headers or allocation API.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hfs
