#include "hfs/manifest.hpp"
#include "hfs/crawler_identity.hpp"
#include "hfs/file_fingerprint.hpp"

#include <cstdio> // CLD2's public header declares APIs using FILE.
#include <public/compact_lang_det.h>
#include <curl/curlver.h>
#include <lexbor/core/base.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <ostream>
#include <stdexcept>

namespace {

using Json = nlohmann::json;

auto stop_name(hfs::CrawlStop stop) -> std::string_view
{
    switch (stop) {
    case hfs::CrawlStop::queue_empty: return "queue_empty";
    case hfs::CrawlStop::page_limit: return "page_limit";
    case hfs::CrawlStop::target_limit: return "target_limit";
    case hfs::CrawlStop::duration_limit: return "duration_limit";
    }
    throw std::runtime_error{"Unknown crawl stop in manifest"};
}

auto language_name(hfs::CrawlLanguagePolicy policy) -> std::string_view
{
    switch (policy) {
    case hfs::CrawlLanguagePolicy::english_only: return "en";
    case hfs::CrawlLanguagePolicy::any: return "any";
    }
    throw std::runtime_error{"Unknown language policy in manifest"};
}

auto http_options(const hfs::HttpOptions& options) -> Json
{
    return {{"connect_timeout_ms", options.connect_timeout_ms}, {"timeout_ms", options.timeout_ms},
            {"max_body_bytes", options.max_body_bytes}};
}

auto artifact(const std::optional<std::string>& path) -> Json
{
    if (!path) {
        return nullptr;
    }
    const auto fingerprint = hfs::fingerprint_file(*path);
    const auto absolute = std::filesystem::absolute(*path).lexically_normal().generic_u8string();
    // JSON strings carry UTF-8 bytes; char8_t is a distinct C++20 character type.
    const std::string utf8{reinterpret_cast<const char*>(absolute.data()), absolute.size()};
    return {{"path", utf8}, {"bytes", fingerprint.bytes}, {"sha256", fingerprint.sha256}};
}

auto unix_ms(std::chrono::system_clock::time_point time) -> std::chrono::milliseconds::rep
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

} // namespace

namespace hfs {

void write_run_manifest(std::ostream& output, const CrawlCommand& command,
                        const CrawlSummary& summary, const RunTiming& timing)
{
    const auto& options = command.options;
    // This section can be saved separately and passed directly to --config.
    const Json configuration{
        {"schema_version", 1}, {"seeds", command.seeds}, {"allowed_hosts", command.allowed_hosts},
        {"max_pages", options.max_pages}, {"max_targets", options.max_targets},
        {"max_depth", options.frontier.max_depth}, {"max_pending", options.frontier.max_pending},
        {"max_urls", options.frontier.max_urls}, {"max_duration_ms", options.max_duration.count()},
        {"delay_ms", options.timing.interval.count()}, {"language", language_name(options.language_policy)}};
    const Json record{
        {"schema_version", 1}, {"completed", true},
        {"application", {{"name", crawler_product}, {"version", version()}}},
        {"build", {{"compiler", "GNU"}, {"compiler_version", HFS_COMPILER_VERSION},
                   {"type", HFS_BUILD_TYPE}, {"cmake_version", HFS_CMAKE_VERSION}, {"cplusplus", __cplusplus}}},
        {"dependencies", {{"curl", LIBCURL_VERSION}, {"lexbor", LEXBOR_VERSION_STRING},
                          {"json", std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." +
                                   std::to_string(NLOHMANN_JSON_VERSION_MINOR) + "." +
                                   std::to_string(NLOHMANN_JSON_VERSION_PATCH)},
                          {"cld2", CLD2::DetectLanguageVersion()}}},
        {"processing", {{"extractor", html_extractor_version}, {"url_normalization", "lexbor-url-v1"},
                        {"language_detection", language_detection_version}}},
        {"configuration", configuration},
        {"additional_options", {{"http", http_options(options.http)},
                                {"robots_http", http_options(options.robots.http)},
                                {"robots_ttl_seconds", options.robots.ttl.count()},
                                {"robots_retry_seconds", options.robots.retry_after.count()},
                                {"max_redirects", options.max_redirects}}},
        {"timing", {{"started_at_unix_ms", unix_ms(timing.started_at)},
                    {"finished_at_unix_ms", unix_ms(timing.finished_at)}, {"elapsed_ms", timing.elapsed.count()}}},
        {"summary", {{"stop", stop_name(summary.stop)}, {"pages", summary.pages},
                     {"targets", summary.targets_processed}, {"fetch_failures", summary.fetch_failures},
                     {"skipped_responses", summary.skipped_responses}, {"empty_texts", summary.empty_texts},
                     {"duplicate_pages", summary.duplicate_pages}, {"duplicate_texts", summary.duplicate_texts},
                     {"non_english_pages", summary.non_english_pages},
                     {"unknown_language_pages", summary.unknown_language_pages},
                     {"rejected_seeds", summary.rejected_seeds}, {"pending_urls", summary.pending_urls}}},
        {"artifacts", {{"corpus", artifact(command.output_path)}, {"journal", artifact(command.log_path)}}}};
    // Strict serialization also rejects invalid UTF-8 before touching the output.
    const auto serialized = record.dump();
    if (!(output << serialized << '\n')) {
        throw std::runtime_error{"Could not write run manifest"};
    }
}

} // namespace hfs
