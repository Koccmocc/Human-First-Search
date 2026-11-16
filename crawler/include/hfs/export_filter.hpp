#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hfs {

inline constexpr std::string_view export_filter_version{"url-export-v1"};

// Exact absolute HTTP(S) URL, or a directory URL ending in /*. No query/fragment.
[[nodiscard]] auto normalize_export_pattern(std::string_view pattern) -> std::optional<std::string>;

class ExportFilter {
public:
    // Owns normalized rules; invalid patterns throw before any crawl requests.
    explicit ExportFilter(std::span<const std::string> patterns);
    // The fetcher supplies a canonical final URL. Queries do not affect export selection.
    [[nodiscard]] auto excludes(std::string_view canonical_url) const -> bool;

private:
    std::vector<std::string> patterns_;
};

} // namespace hfs
