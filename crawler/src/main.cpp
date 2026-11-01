#include "hfs/cli.hpp"

#include <iostream>
#include <string_view>
#include <vector>

auto main(int argc, char* argv[]) -> int
{
    // argv owns these strings until main returns; no string copies are needed.
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto action = hfs::parse_arguments(arguments);
    if (const auto* error = std::get_if<hfs::CliError>(&action)) {
        std::cerr << "error: " << error->message << '\n';
        return 2;
    }

    switch (std::get<hfs::CliAction>(action)) {
    case hfs::CliAction::help:
        std::cout << hfs::help_text();
        break;
    case hfs::CliAction::version:
        std::cout << "hfs-crawler " << hfs::version() << '\n';
        break;
    }

    return 0;
}
