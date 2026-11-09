#pragma once

#include "hfs/crawl.hpp"

#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace hfs {

enum class CliAction { help, version };

struct CliError {
    std::string message;
};

struct CrawlCommand {
    std::vector<std::string> seeds;
    std::vector<std::string> allowed_hosts;
    CrawlOptions options;
    std::optional<std::string> output_path;
};

// Parsed commands and errors own their strings; argv can be released afterwards.
using CliResult = std::variant<CliAction, CrawlCommand, CliError>;

// Views are borrowed only for this call. Validation performs no network requests.
[[nodiscard]] auto parse_arguments(std::span<const std::string_view> arguments)
    -> CliResult;

[[nodiscard]] auto help_text() noexcept -> std::string_view;
[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace hfs
