#include "hfs/host_delay.hpp"
#include "hfs/url.hpp"

#include <stdexcept>
#include <utility>

namespace hfs {

HostDelay::HostDelay(HostDelayOptions options) : options_{std::move(options)}
{
    if (options_.interval < std::chrono::milliseconds::zero() || !options_.now || !options_.sleep) {
        throw std::invalid_argument{"Expected a non-negative host interval and clock/sleep functions"};
    }
}

void HostDelay::wait(std::string_view url)
{
    const auto host = http_url_host(url);
    if (!host) {
        throw std::invalid_argument{"Host delay requires an absolute HTTP(S) URL"};
    }

    const auto current = options_.now();
    const auto previous = last_started_.find(*host);
    if (previous != last_started_.end()) {
        // Rounding elapsed time down may add a fraction of a millisecond of delay.
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current - previous->second);
        if (elapsed < options_.interval) {
            options_.sleep(options_.interval - elapsed);
        }
    }
    last_started_.insert_or_assign(*host, options_.now());
}

} // namespace hfs
