#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hfs {

// Resolve against an absolute HTTP(S) base and serialize without a fragment.
// Reject parser failures, other schemes and credentials. No network access occurs.
[[nodiscard]] auto resolve_http_url(std::string_view base, std::string_view reference)
    -> std::optional<std::string>;

// Return the parser's canonical host (without port), e.g. example.org or [::1].
[[nodiscard]] auto http_url_host(std::string_view url) -> std::optional<std::string>;

// Locate /robots.txt at the page's HTTP(S) scheme/host/port, not its directory.
// The canonical result can key the rules cache; invalid URLs are rejected.
[[nodiscard]] auto robots_txt_url(std::string_view page_url) -> std::optional<std::string>;

// Entries must be canonical hosts, as returned by http_url_host(). Empty denies all.
// Subdomains and trailing dots remain distinct. This host-only policy ignores ports.
[[nodiscard]] auto is_allowed_host(std::string_view url,
                                  std::span<const std::string_view> allowed_hosts) -> bool;

} // namespace hfs
