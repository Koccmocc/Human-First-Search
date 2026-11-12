#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hfs {

inline constexpr std::string_view language_detection_version{"cld2-compact-v1"};
inline constexpr std::size_t language_min_characters{200};
inline constexpr std::size_t language_max_bytes{2 * 1024 * 1024};

enum class LanguageStatus { detected, too_short, unreliable, invalid_text, too_large };

struct LanguageResult {
    LanguageStatus status{LanguageStatus::unreliable};
    // Present only for detected results; owns the CLD2 code, e.g. en, de or uk.
    std::optional<std::string> code;
    // Estimated dominant-language text share, not a confidence probability.
    // Unknown results have no code and a zero percent.
    int percent{};
};

// Analyze extracted plain text without HTML, URL or language hints.
// Oversized input is rejected before reading it; text is never truncated or repaired.
// Validate UTF-8 and reject NUL, then require 200 Unicode scalar values before
// calling CLD2's checked API. Additional CLD2 interchange restrictions may reject text.
// Only reliable, consistent dominant results expose a code. This is not an English
// admission policy. Allocation or dependency exceptions propagate.
[[nodiscard]] auto detect_language(std::string_view text) -> LanguageResult;

} // namespace hfs
