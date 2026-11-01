#pragma once

#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace hfs {

enum class CliAction { help, version };

struct CliError {
    std::string message;
};

// Exactly one alternative is active: an action or an owning error diagnostic.
using CliResult = std::variant<CliAction, CliError>;

// Views are borrowed only for this call; errors own their diagnostic text.
[[nodiscard]] auto parse_arguments(std::span<const std::string_view> arguments)
    -> CliResult;

[[nodiscard]] auto help_text() noexcept -> std::string_view;
[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace hfs
