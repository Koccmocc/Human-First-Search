#include "hfs/cli.hpp"
#include "hfs/crawl_log.hpp"
#include "hfs/jsonl.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void open_jsonl_file(std::ofstream& stream, const std::optional<std::string>& path)
{
    if (!path) {
        return;
    }
    // Check each destination immediately before opening, including the second file.
    // This also rejects a log path that aliases the newly opened corpus file.
    if (std::filesystem::exists(std::filesystem::symlink_status(*path))) {
        throw std::runtime_error{"JSONL output path already exists: " + *path};
    }
    // Binary mode keeps LF line endings identical on Windows and Unix.
    stream.open(*path, std::ios::out | std::ios::binary);
    if (!stream) {
        throw std::runtime_error{"Could not open JSONL output file: " + *path};
    }
}

void close_jsonl_file(std::ofstream& stream)
{
    if (stream.is_open()) {
        stream.close();
        if (!stream) {
            throw std::runtime_error{"Could not close JSONL output file"};
        }
    }
}

auto stop_name(hfs::CrawlStop stop) noexcept -> std::string_view
{
    switch (stop) {
    case hfs::CrawlStop::queue_empty: return "queue_empty";
    case hfs::CrawlStop::page_limit: return "page_limit";
    case hfs::CrawlStop::target_limit: return "target_limit";
    case hfs::CrawlStop::duration_limit: return "duration_limit";
    }
    return "unknown";
}

auto run(int argc, char* argv[]) -> int
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

    if (const auto* command = std::get_if<hfs::CrawlCommand>(&action)) {
        std::ofstream output;
        std::ofstream log;
        open_jsonl_file(output, command->output_path);
        open_jsonl_file(log, command->log_path);
        auto options = command->options;
        if (log.is_open()) {
            options.on_event = [&log](const auto& event) { hfs::write_crawl_event_jsonl(log, event); };
        }
        const auto summary = hfs::crawl(command->seeds, command->allowed_hosts, [&output](const auto& page) {
            if (output.is_open()) {
                hfs::write_page_jsonl(output, page);
            }
            if (!(std::cout << page.url << '\n')) {
                throw std::runtime_error{"Could not write page URL to stdout"};
            }
        }, options);
        close_jsonl_file(output);
        close_jsonl_file(log);
        std::cout.flush();
        if (!std::cout) {
            throw std::runtime_error{"Could not flush page URLs to stdout"};
        }
        std::cerr << "crawl: stop=" << stop_name(summary.stop)
                  << " pages=" << summary.pages << " targets=" << summary.targets_processed
                  << " fetch_failures=" << summary.fetch_failures
                  << " skipped_responses=" << summary.skipped_responses
                  << " empty_texts=" << summary.empty_texts
                  << " duplicate_pages=" << summary.duplicate_pages
                  << " duplicate_texts=" << summary.duplicate_texts
                  << " non_english_pages=" << summary.non_english_pages
                  << " unknown_language_pages=" << summary.unknown_language_pages
                  << " rejected_seeds=" << summary.rejected_seeds
                  << " pending_urls=" << summary.pending_urls << '\n';
        return std::cerr ? 0 : 1;
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

} // namespace

auto main(int argc, char* argv[]) -> int
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
