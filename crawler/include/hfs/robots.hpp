#pragma once

#include <string_view>

namespace hfs {

// Match successfully obtained robots.txt text for our crawler's product token.
// Normalize the absolute HTTP(S) URL first; invalid URLs are denied. Empty rules
// permit access, so callers must not pass empty text to represent a fetch failure.
[[nodiscard]] auto robots_allows(std::string_view robots_txt, std::string_view url) -> bool;

} // namespace hfs
