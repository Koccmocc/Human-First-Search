#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hfs {

// Resolve against an absolute HTTP(S) base and serialize without a fragment.
// Reject parser failures, other schemes and credentials. No network access occurs.
[[nodiscard]] auto resolve_http_url(std::string_view base, std::string_view reference)
    -> std::optional<std::string>;

} // namespace hfs
