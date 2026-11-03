#pragma once

#include <string_view>

namespace hfs {

// Use the same product token in HTTP User-Agent and robots.txt group selection.
inline constexpr std::string_view crawler_product = "HumanFirstSearch";

} // namespace hfs
