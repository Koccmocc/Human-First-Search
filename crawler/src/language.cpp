#include "hfs/language.hpp"

// CLD2's public header declares FILE* without including its declaration.
#include <cstdio>
#include <public/compact_lang_det.h>
#include <public/encodings.h>

#include <lexbor/encoding/encoding.h>

#include <limits>

namespace {

auto character_count(std::string_view text) -> std::optional<std::size_t>
{
    const auto* next = reinterpret_cast<const lxb_char_t*>(text.empty() ? "" : text.data());
    const auto* end = next + text.size();
    lxb_encoding_decode_t decoder{};
    std::size_t count{};
    while (next != end) {
        const auto cp = lxb_encoding_decode_utf_8_single(&decoder, &next, end);
        if (cp == 0 || cp > LXB_ENCODING_MAX_CODEPOINT) {
            return std::nullopt;
        }
        ++count;
    }
    return count;
}

} // namespace

namespace hfs {

auto detect_language(std::string_view text) -> LanguageResult
{
    // The hard byte cap also makes CLD2's signed int length conversion safe.
    static_assert(language_max_bytes <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    if (text.size() > language_max_bytes) {
        return {LanguageStatus::too_large, std::nullopt, 0};
    }
    const auto characters = character_count(text);
    if (!characters) {
        return {LanguageStatus::invalid_text, std::nullopt, 0};
    }
    if (*characters < language_min_characters) {
        return {LanguageStatus::too_short, std::nullopt, 0};
    }

    const CLD2::CLDHints hints{nullptr, nullptr, CLD2::UNKNOWN_ENCODING, CLD2::UNKNOWN_LANGUAGE};
    CLD2::Language top[3]{CLD2::UNKNOWN_LANGUAGE, CLD2::UNKNOWN_LANGUAGE, CLD2::UNKNOWN_LANGUAGE};
    int percent[3]{};
    double scores[3]{};
    int text_bytes{};
    int valid_prefix{};
    bool reliable{};
    const auto length = static_cast<int>(text.size());
    const auto language = CLD2::ExtDetectLanguageSummaryCheckUTF8(
        text.data(), length, true, &hints, 0, top, percent, scores, nullptr,
        &text_bytes, &reliable, &valid_prefix);
    if (valid_prefix != length) {
        return {LanguageStatus::invalid_text, std::nullopt, 0};
    }
    // Discard guesses when CLD2 cannot provide a consistent dominant language.
    if (!reliable || language == CLD2::UNKNOWN_LANGUAGE || language == CLD2::TG_UNKNOWN_LANGUAGE || top[0] != language ||
        text_bytes <= 0 || percent[0] <= 0 || percent[0] > 100) {
        return {LanguageStatus::unreliable, std::nullopt, 0};
    }
    return {LanguageStatus::detected, std::string{CLD2::LanguageCode(language)}, percent[0]};
}

} // namespace hfs
