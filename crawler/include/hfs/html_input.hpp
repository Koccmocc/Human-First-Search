#pragma once

#include <string_view>

namespace hfs {

enum class HtmlInputStatus { ready, not_html, invalid_content_type, unsupported_encoding, invalid_text };

// A UTF-8-only input policy, not a browser encoding detector or transcoder.
// Checks HTTP metadata, BOM, early HTML meta declarations and the entire body.
// invalid_text includes malformed UTF-8 and NUL bytes. Lexbor setup errors throw.
[[nodiscard]] auto check_html_input(std::string_view content_type, std::string_view body)
    -> HtmlInputStatus;

} // namespace hfs
