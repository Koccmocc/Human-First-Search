#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace hfs {

struct HostDelayOptions {
    using Clock = std::chrono::steady_clock;
    std::chrono::milliseconds interval{1000};
    std::function<Clock::time_point()> now = Clock::now;
    std::function<void(std::chrono::milliseconds)> sleep = [](auto duration) {
        std::this_thread::sleep_for(duration);
    };
};

// Spaces request attempts by canonical host name during a single-threaded crawl.
class HostDelay {
public:
    explicit HostDelay(HostDelayOptions options = {});
    void wait(std::string_view url);

private:
    HostDelayOptions options_;
    std::unordered_map<std::string, HostDelayOptions::Clock::time_point> last_started_;
};

} // namespace hfs
