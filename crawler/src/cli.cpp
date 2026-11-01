#include "hfs/cli.hpp"

namespace hfs {

auto parse_arguments(std::span<const std::string_view> arguments)
    -> CliResult
{
    if (arguments.empty()) {
        return CliAction::help;
    }

    // Reject extra arguments rather than silently ignoring a user's mistake.
    if (arguments.size() != 1) {
        return CliError{"Expected a single option. Use --help for usage."};
    }

    const auto option = arguments.front();
    if (option == "--help" || option == "-h") {
        return CliAction::help;
    }
    if (option == "--version") {
        return CliAction::version;
    }

    return CliError{"Unknown argument: " + std::string{option}};
}

auto help_text() noexcept -> std::string_view
{
    return "Human-first Search crawler\n"
           "Usage: hfs-crawler [--help | --version]\n"
           "\n"
           "Options:\n"
           "  -h, --help  Show this help message.\n"
           "  --version   Show the application version.\n"
           "\n"
           "This scaffold does not crawl pages or export a corpus yet.\n";
}

auto version() noexcept -> std::string_view
{
    return HFS_VERSION;
}

} // namespace hfs
