#include "hfs/config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct ConfigField { std::string_view key; std::string_view option; };
constexpr std::array limits{
    ConfigField{"max_pages", "--max-pages"}, ConfigField{"max_targets", "--max-targets"},
    ConfigField{"max_depth", "--max-depth"}, ConfigField{"max_pending", "--max-pending"},
    ConfigField{"max_urls", "--max-urls"}, ConfigField{"max_duration_ms", "--max-duration-ms"},
    ConfigField{"delay_ms", "--delay-ms"}};

} // namespace

namespace hfs {

auto parse_crawl_config(std::string_view input) -> CrawlConfigResult
{
    if (input.size() > crawl_config_max_bytes) {
        return CliError{"Crawl config exceeds 64 KiB"};
    }
    try {
        std::unordered_set<std::string> keys;
        const auto config = nlohmann::json::parse(input, [&](int depth, auto event, auto& value) {
            // This flat schema needs only a root object and arrays of scalar values.
            if (depth > 2 || (event == nlohmann::json::parse_event_t::object_start && depth != 0)) {
                throw std::invalid_argument{"Nested crawl config structures are not supported"};
            }
            if (event == nlohmann::json::parse_event_t::key &&
                !keys.insert(value.template get<std::string>()).second) {
                throw std::invalid_argument{"Duplicate crawl config key"};
            }
            return true;
        });
        if (!config.is_object() || !config.contains("schema_version") ||
            !config.at("schema_version").is_number_integer() || config.at("schema_version") != 1) {
            return CliError{"Crawl config requires an object with integer schema_version 1"};
        }
        for (const auto& entry : config.items()) {
            const auto& key = entry.key();
            if (key != "schema_version" && key != "seeds" && key != "allowed_hosts" && key != "language" &&
                std::none_of(limits.begin(), limits.end(), [&](const auto& field) { return field.key == key; })) {
                return CliError{"Unknown crawl config key: " + key};
            }
        }
        std::vector<std::string> words{"crawl"};
        for (const auto field : {ConfigField{"seeds", "--seed"}, ConfigField{"allowed_hosts", "--allow-host"}}) {
            if (!config.contains(field.key) || !config.at(field.key).is_array()) {
                return CliError{"Crawl config requires an array: " + std::string{field.key}};
            }
            for (const auto& value : config.at(field.key)) {
                if (!value.is_string()) {
                    return CliError{"Crawl config array must contain strings: " + std::string{field.key}};
                }
                words.emplace_back(field.option);
                words.push_back(value.get<std::string>());
            }
        }
        for (const auto& field : limits) {
            if (config.contains(field.key)) {
                if (!config.at(field.key).is_number_integer()) {
                    return CliError{"Crawl config limit must be an integer: " + std::string{field.key}};
                }
                words.emplace_back(field.option);
                words.push_back(config.at(field.key).dump());
            }
        }
        if (config.contains("language")) {
            if (!config.at("language").is_string()) {
                return CliError{"Crawl config language must be en or any"};
            }
            words.emplace_back("--language");
            words.push_back(config.at("language").get<std::string>());
        }
        // Construct views only after all owning strings are stable. Reuse the
        // canonical URL/host and numeric validation without executing a shell.
        const std::vector<std::string_view> arguments{words.begin(), words.end()};
        auto parsed = parse_arguments(arguments);
        if (auto* command = std::get_if<CrawlCommand>(&parsed)) {
            return std::move(*command);
        }
        return std::move(std::get<CliError>(parsed));
    } catch (const nlohmann::json::exception& error) {
        return CliError{"Invalid crawl config JSON: " + std::string{error.what()}};
    } catch (const std::invalid_argument& error) {
        return CliError{error.what()};
    }
}

auto load_crawl_config(std::string_view path) -> CrawlConfigResult
{
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        return CliError{"Crawl config requires a nonempty file path without NUL"};
    }
    std::ifstream file{std::string{path}, std::ios::binary};
    if (!file) {
        return CliError{"Could not open crawl config: " + std::string{path}};
    }
    std::string input(crawl_config_max_bytes + 1, '\0');
    file.read(input.data(), static_cast<std::streamsize>(input.size()));
    const auto bytes = static_cast<std::size_t>(file.gcount());
    if (bytes > crawl_config_max_bytes) {
        return CliError{"Crawl config exceeds 64 KiB"};
    }
    if (file.bad() || (!file && !file.eof())) {
        return CliError{"Could not read crawl config: " + std::string{path}};
    }
    input.resize(bytes);
    return parse_crawl_config(input);
}

} // namespace hfs
