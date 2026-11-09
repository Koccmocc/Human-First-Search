#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hfs {

struct PageIdentity {
    std::string page_id;
    std::string text_hash;
};

// Both values use "sha256:" plus 64 lowercase hex digits. The page ID hashes the
// canonical HTTP(S) URL; text_hash hashes the exact supplied bytes, without repair
// or extra normalization. Supply extracted UTF-8 text for corpus records.
// Invalid/credential-bearing URLs return nullopt. Hash backend failures throw.
[[nodiscard]] auto make_page_identity(std::string_view final_url, std::string_view text)
    -> std::optional<PageIdentity>;

} // namespace hfs
