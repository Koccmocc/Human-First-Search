#pragma once

#include <string>
#include <string_view>

namespace hfs::detail {

[[nodiscard]] auto sha256(std::string_view input) -> std::string;

} // namespace hfs::detail
