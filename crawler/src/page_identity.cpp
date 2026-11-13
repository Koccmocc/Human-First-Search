#include "hfs/page_identity.hpp"
#include "hfs/url.hpp"
#include "sha256.hpp"

namespace hfs {

auto make_page_identity(std::string_view final_url, std::string_view text)
    -> std::optional<PageIdentity>
{
    const auto canonical_url = resolve_http_url(final_url, "");
    if (!canonical_url) {
        return std::nullopt;
    }
    return PageIdentity{detail::sha256(*canonical_url), detail::sha256(text)};
}

} // namespace hfs
