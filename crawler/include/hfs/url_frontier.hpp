#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hfs {

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

// A single-threaded FIFO. Accepted URLs stay known for the lifetime of the frontier.
class UrlFrontier {
public:
    explicit UrlFrontier(std::vector<std::string> allowed_hosts, FrontierLimits limits = {});

    [[nodiscard]] auto add_seed(std::string_view url) -> EnqueueResult;
    [[nodiscard]] auto add_link(const CrawlTarget& parent, std::string_view reference) -> EnqueueResult;
    [[nodiscard]] auto pop() -> std::optional<CrawlTarget>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto accepted_count() const noexcept -> std::size_t;

private:
    auto enqueue(std::string_view base, std::string_view reference, std::size_t depth) -> EnqueueResult;

    std::vector<std::string> allowed_hosts_;
    FrontierLimits limits_;
    std::deque<CrawlTarget> pending_;
    std::unordered_set<std::string> known_;
};

} // namespace hfs
