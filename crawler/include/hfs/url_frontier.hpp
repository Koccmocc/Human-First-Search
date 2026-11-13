#pragma once

#include <cstddef>
#include <deque>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hfs {

inline constexpr std::string_view frontier_policy_version{"depth-host-round-robin-v1"};

struct FrontierLimits {
    std::size_t max_pending{1000};
    std::size_t max_urls{5000};
    std::size_t max_depth{3};
};

struct CrawlTarget {
    std::string url;
    std::size_t depth{};
};

enum class EnqueueResult { added, duplicate, invalid_url, out_of_scope, depth_limit, queue_full, url_limit };

// Single-threaded breadth-first traversal, with round-robin hosts at each depth.
// Each host/depth keeps FIFO order. Accepted URLs stay known after popping.
class UrlFrontier {
public:
    explicit UrlFrontier(std::vector<std::string> allowed_hosts, FrontierLimits limits = {});

    [[nodiscard]] auto add_seed(std::string_view url) -> EnqueueResult;
    [[nodiscard]] auto add_link(const CrawlTarget& parent, std::string_view reference) -> EnqueueResult;
    [[nodiscard]] auto pop() -> std::optional<CrawlTarget>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto accepted_count() const noexcept -> std::size_t;

private:
    struct HostQueue {
        std::string host;
        std::deque<CrawlTarget> targets;
    };

    auto enqueue(std::string_view base, std::string_view reference, std::size_t depth) -> EnqueueResult;

    std::vector<std::string> allowed_hosts_;
    FrontierLimits limits_;
    // Only nonempty depths/hosts are retained. List splicing rotates without allocation.
    std::map<std::size_t, std::list<HostQueue>> pending_;
    std::size_t pending_count_{};
    std::unordered_set<std::string> known_;
};

} // namespace hfs
