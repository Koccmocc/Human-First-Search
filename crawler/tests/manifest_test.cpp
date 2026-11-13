#include "hfs/manifest.hpp"
#include "hfs/config.hpp"
#include "hfs/file_fingerprint.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <stdexcept>

namespace {
auto sample_command() -> hfs::CrawlCommand
{
    hfs::CrawlCommand command;
    command.seeds = {"https://example.org/second", "https://example.org/first"};
    command.allowed_hosts = {"example.org"};
    return command;
}
}

TEST(Manifest, RecordsEffectiveDefaultsAndReplayableConfigurationWithSuppliedTiming)
{
    auto command = sample_command();
    command.options.max_pages = 7;
    command.options.max_targets = 12;
    command.options.frontier.max_depth = 0;
    command.options.timing.interval = std::chrono::milliseconds{321};
    command.options.http.timeout_ms = 1234;
    command.options.language_policy = hfs::CrawlLanguagePolicy::any;
    hfs::CrawlSummary summary;
    summary.stop = hfs::CrawlStop::page_limit;
    summary.pages = 7;
    summary.targets_processed = 12;
    summary.pending_urls = 4;
    const hfs::RunTiming timing{std::chrono::system_clock::time_point{std::chrono::milliseconds{1000}},
        std::chrono::system_clock::time_point{std::chrono::milliseconds{4000}}, std::chrono::milliseconds{2900}};
    std::ostringstream output;
    hfs::write_run_manifest(output, command, summary, timing);
    const auto record = nlohmann::json::parse(output.str());
    EXPECT_EQ(record.at("schema_version"), 1);
    EXPECT_EQ(record.at("completed"), true);
    EXPECT_EQ(record["summary"]["stop"], "page_limit");
    EXPECT_EQ(record["summary"]["pages"], 7);
    EXPECT_EQ(record["summary"]["targets"], 12);
    EXPECT_EQ(record["summary"]["pending_urls"], 4);
    EXPECT_EQ(record["timing"]["started_at_unix_ms"], 1000);
    EXPECT_EQ(record["timing"]["finished_at_unix_ms"], 4000);
    EXPECT_EQ(record["timing"]["elapsed_ms"], 2900); // Steady time is supplied independently.
    EXPECT_TRUE(record["artifacts"]["corpus"].is_null());
    EXPECT_TRUE(record["artifacts"]["journal"].is_null());
    EXPECT_EQ(record["processing"]["extractor"], hfs::html_extractor_version);
    EXPECT_EQ(record["processing"]["frontier"], hfs::frontier_policy_version);
    EXPECT_EQ(record["processing"]["language_detection"], hfs::language_detection_version);
    EXPECT_EQ(record["build"]["cplusplus"], 202002L);
    EXPECT_FALSE(record["dependencies"]["cld2"].get<std::string>().empty());
    EXPECT_EQ(record["additional_options"]["http"]["timeout_ms"], 1234);
    EXPECT_EQ(record["additional_options"]["robots_ttl_seconds"], 86400);
    const auto replay = hfs::parse_crawl_config(record.at("configuration").dump());
    ASSERT_TRUE(std::holds_alternative<hfs::CrawlCommand>(replay));
    const auto& restored = std::get<hfs::CrawlCommand>(replay);
    EXPECT_EQ(restored.seeds, command.seeds);
    EXPECT_EQ(restored.allowed_hosts, command.allowed_hosts);
    EXPECT_EQ(restored.options.max_pages, 7U);
    EXPECT_EQ(restored.options.max_targets, 12U);
    EXPECT_EQ(restored.options.frontier.max_depth, 0U);
    EXPECT_EQ(restored.options.frontier.max_pending, command.options.frontier.max_pending);
    EXPECT_EQ(restored.options.frontier.max_urls, command.options.frontier.max_urls);
    EXPECT_EQ(restored.options.max_duration, command.options.max_duration);
    EXPECT_EQ(restored.options.timing.interval, command.options.timing.interval);
    EXPECT_EQ(restored.options.language_policy, hfs::CrawlLanguagePolicy::any);
    EXPECT_EQ(output.str().back(), '\n');
}

TEST(Manifest, RecordsEveryStopReasonWithoutInventingPageSuccess)
{
    for (const auto& [stop, name] : {std::pair{hfs::CrawlStop::queue_empty, "queue_empty"},
                                   std::pair{hfs::CrawlStop::page_limit, "page_limit"},
                                   std::pair{hfs::CrawlStop::target_limit, "target_limit"},
                                   std::pair{hfs::CrawlStop::duration_limit, "duration_limit"}}) {
        hfs::CrawlSummary summary;
        summary.stop = stop;
        std::ostringstream output;
        hfs::write_run_manifest(output, sample_command(), summary, {});
        const auto record = nlohmann::json::parse(output.str());
        EXPECT_EQ(record["summary"]["stop"], name);
        EXPECT_EQ(record["summary"]["pages"], 0);
        EXPECT_EQ(record["configuration"]["language"], "en");
    }
}

TEST(Manifest, RejectsArtifactAndSerializationFailuresBeforeWriting)
{
    auto command = sample_command();
    command.output_path = "";
    std::ostringstream output;
    output << "previous\n";
    EXPECT_THROW(hfs::write_run_manifest(output, command, {}, {}), std::runtime_error);
    EXPECT_EQ(output.str(), "previous\n");
    EXPECT_THROW((void)hfs::fingerprint_file({}), std::runtime_error);
    command.output_path.reset();
    command.allowed_hosts = {"broken\xFF"};
    EXPECT_THROW(hfs::write_run_manifest(output, command, {}, {}), nlohmann::json::type_error);
    EXPECT_EQ(output.str(), "previous\n");
}

TEST(Manifest, PropagatesFailedAndShortOutputWrites)
{
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    EXPECT_THROW(hfs::write_run_manifest(failed, sample_command(), {}, {}), std::runtime_error);
    class ShortBuffer : public std::streambuf {
        auto xsputn(const char*, std::streamsize count) -> std::streamsize override
        {
            return count > 0 ? count - 1 : 0;
        }
    } buffer;
    std::ostream partial{&buffer};
    EXPECT_THROW(hfs::write_run_manifest(partial, sample_command(), {}, {}), std::runtime_error);
}
