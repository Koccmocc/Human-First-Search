#include "hfs/cli.hpp"
#include "hfs/url.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <unordered_set>
#include <utility>

namespace {

auto count(std::string_view value, bool allow_zero) -> std::optional<std::size_t>
{
    if (value.empty()) {
        return std::nullopt;
    }
    std::size_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        (!allow_zero && result == 0)) {
        return std::nullopt;
    }
    return result;
}

auto canonical_host(std::string_view value) -> std::optional<std::string>
{
    // Accept a hostname or bracketed IPv6, never a URL, credentials or port.
    if (value.empty() || value.find_first_of("/\\?#@") != std::string_view::npos ||
        (value.find(':') != std::string_view::npos &&
         !(value.starts_with('[') && value.ends_with(']'))) ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch <= 0x20 || ch == 0x7F; })) {
        return std::nullopt;
    }
    return hfs::http_url_host("http://" + std::string{value} + "/");
}

auto parse_crawl(std::span<const std::string_view> arguments) -> hfs::CliResult
{
    hfs::CrawlCommand command;
    struct Limit { std::string_view name; std::size_t* value; bool allow_zero; };
    const std::array limits{
        Limit{"--max-pages", &command.options.max_pages, false},
        Limit{"--max-targets", &command.options.max_targets, false},
        Limit{"--max-depth", &command.options.frontier.max_depth, true},
        Limit{"--max-pending", &command.options.frontier.max_pending, false},
        Limit{"--max-urls", &command.options.frontier.max_urls, false}};
    std::unordered_set<std::string_view> supplied;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        const auto limit = std::find_if(limits.begin(), limits.end(),
                                         [option](const auto& item) { return item.name == option; });
        if (option != "--seed" && option != "--allow-host" && option != "--delay-ms" &&
            option != "--output" && option != "--log" && limit == limits.end()) {
            return hfs::CliError{"Unknown argument: " + std::string{option}};
        }
        if (++index == arguments.size() || arguments[index].starts_with("--")) {
            return hfs::CliError{"Missing value for " + std::string{option}};
        }
        const auto value = arguments[index];
        if (option == "--seed") {
            auto url = hfs::resolve_http_url(value, "");
            if (!url) {
                return hfs::CliError{"--seed requires an absolute HTTP(S) URL without credentials"};
            }
            command.seeds.push_back(std::move(*url));
        } else if (option == "--allow-host") {
            auto host = canonical_host(value);
            if (!host) {
                return hfs::CliError{"--allow-host requires a host name without scheme, path or port"};
            }
            if (std::find(command.allowed_hosts.begin(), command.allowed_hosts.end(), *host) == command.allowed_hosts.end()) {
                command.allowed_hosts.push_back(std::move(*host));
            }
        } else if (option == "--output" || option == "--log") {
            auto& path = option == "--output" ? command.output_path : command.log_path;
            if (path || value.empty() || value.find('\0') != std::string_view::npos) {
                return hfs::CliError{std::string{option} + " requires one nonempty file path and cannot be repeated"};
            }
            path = value;
        } else {
            if (!supplied.insert(option).second) {
                return hfs::CliError{"Duplicate option: " + std::string{option}};
            }
            const auto number = count(value, option == "--delay-ms" || limit->allow_zero);
            if (!number) {
                return hfs::CliError{"Invalid decimal value for " + std::string{option}};
            }
            if (option == "--delay-ms") {
                using Milliseconds = std::chrono::milliseconds;
                if (!std::in_range<Milliseconds::rep>(*number)) {
                    return hfs::CliError{"Value out of range for --delay-ms"};
                }
                command.options.timing.interval = Milliseconds{static_cast<Milliseconds::rep>(*number)};
            } else {
                *limit->value = *number;
            }
        }
    }
    if (command.seeds.empty() || command.allowed_hosts.empty()) {
        return hfs::CliError{"crawl requires at least one --seed and one --allow-host"};
    }
    // Validate all seeds after parsing so the order of seed/host options is irrelevant.
    const std::vector<std::string_view> hosts{command.allowed_hosts.begin(), command.allowed_hosts.end()};
    for (const auto& seed : command.seeds) {
        if (!hfs::is_allowed_host(seed, hosts)) {
            return hfs::CliError{"Seed host is not allowed: " + seed};
        }
    }
    return command;
}

} // namespace

namespace hfs {

auto parse_arguments(std::span<const std::string_view> arguments)
    -> CliResult
{
    if (arguments.empty()) {
        return CliAction::help;
    }

    const auto option = arguments.front();
    if (option == "crawl") {
        if (arguments.size() == 2 && (arguments[1] == "--help" || arguments[1] == "-h")) {
            return CliAction::help;
        }
        return parse_crawl(arguments.subspan(1));
    }
    if ((option == "--help" || option == "-h" || option == "--version") && arguments.size() != 1) {
        return CliError{"Expected a single option. Use --help for usage."};
    }
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
           "       hfs-crawler crawl --seed URL --allow-host HOST [options]\n"
           "\n"
           "Options:\n"
           "  -h, --help  Show this help message.\n"
           "  --version   Show the application version.\n"
           "\n"
           "Crawl options (separate each option from its value):\n"
           "  --seed URL          Absolute HTTP(S) seed; repeatable and required.\n"
           "  --allow-host HOST   Explicit host without scheme/path/port; repeatable and required.\n"
           "  --max-pages N       Unique-text HTML page limit (default 1000).\n"
           "  --max-targets N     Processed queue entry limit (default 5000).\n"
           "  --max-depth N       Link depth; seeds are zero (default 3).\n"
           "  --max-pending N     Pending queue capacity (default 1000).\n"
           "  --max-urls N        Total admitted URL limit (default 5000).\n"
           "  --delay-ms N        Per-host request interval (default 1000; zero for local tests).\n"
           "  --output PATH       Write a new JSONL file; parent directory must exist.\n"
           "  --log PATH          Write a new crawl-event JSONL file.\n"
           "\n"
           "Numbers are unsigned decimal; only depth and delay may be zero.\n"
           "Outputs final page URLs to stdout and a crawl summary to stderr.\n"
           "Uses robots.txt, UTF-8 HTML checks, default HTTP timeouts and five redirects.\n"
           "Keeps the first page per text SHA-256; duplicate pages still supply links.\n"
           "With --output, also writes page records as JSONL; existing paths are rejected.\n"
           "With --log, writes completed crawl targets to a separate new JSONL file.\n";
}

auto version() noexcept -> std::string_view
{
    return HFS_VERSION;
}

} // namespace hfs
