#include "hfs/export_filter.hpp"
#include "hfs/url.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace hfs {

auto normalize_export_pattern(std::string_view pattern) -> std::optional<std::string>
{
    const bool subtree = pattern.ends_with("/*");
    if (subtree) {
        pattern.remove_suffix(1);
    }
    if (pattern.empty() || pattern.find_first_of("*?#\\") != std::string_view::npos ||
        std::ranges::any_of(pattern, [](unsigned char ch) { return ch <= 0x20 || ch == 0x7F; })) {
        return std::nullopt;
    }
    auto normalized = resolve_http_url(pattern, "");
    if (normalized && subtree) {
        *normalized += '*';
    }
    return normalized;
}

ExportFilter::ExportFilter(std::span<const std::string> patterns)
{
    for (const auto& pattern : patterns) {
        auto normalized = normalize_export_pattern(pattern);
        if (!normalized) {
            throw std::invalid_argument{"Expected an absolute export exclusion URL or directory ending in /*"};
        }
        patterns_.push_back(std::move(*normalized));
    }
}

auto ExportFilter::excludes(std::string_view canonical_url) const -> bool
{
    const auto path_url = canonical_url.substr(0, canonical_url.find('?'));
    return std::ranges::any_of(patterns_, [path_url](std::string_view pattern) {
        if (pattern.ends_with('*')) {
            pattern.remove_suffix(1);
            return path_url.starts_with(pattern);
        }
        return path_url == pattern;
    });
}

} // namespace hfs
