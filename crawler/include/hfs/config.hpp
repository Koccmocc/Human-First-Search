#pragma once

#include "hfs/cli.hpp"

#include <cstddef>
#include <string_view>
#include <variant>

namespace hfs {

inline constexpr std::size_t crawl_config_max_bytes{64 * 1024};
using CrawlConfigResult = std::variant<CrawlCommand, CliError>;

// Schema 1 JSON maps to existing CLI validation, including scope checks.
// No network or file access. Returned values own their data.
[[nodiscard]] auto parse_crawl_config(std::string_view json) -> CrawlConfigResult;

// Read at most the size cap plus one byte. Paths are relative to the current
// working directory. File/schema errors are CliError; allocation failures propagate.
[[nodiscard]] auto load_crawl_config(std::string_view path) -> CrawlConfigResult;

} // namespace hfs
