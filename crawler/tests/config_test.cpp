#include "hfs/config.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {
constexpr std::string_view minimal{
    R"({"schema_version":1,"seeds":["https://example.org/"],"allowed_hosts":["example.org"]})"};
}

TEST(Config, ParsesOwningCanonicalOptionsInSeedOrder)
{
    std::string input{R"({"schema_version":1,
        "seeds":["HTTPS://EXAMPLE.org:443/first#part","https://example.org/second"],
        "allowed_hosts":["EXAMPLE.org","example.org"],"max_pages":8,"max_targets":24,
        "max_depth":0,"max_pending":100,"max_urls":110,"max_duration_ms":60000,
        "delay_ms":0,"language":"any"})"};
    const auto parsed = hfs::parse_crawl_config(input);
    input.assign("Input released");
    ASSERT_TRUE(std::holds_alternative<hfs::CrawlCommand>(parsed));
    const auto& command = std::get<hfs::CrawlCommand>(parsed);
    EXPECT_EQ(command.seeds, (std::vector<std::string>{"https://example.org/first", "https://example.org/second"}));
    EXPECT_EQ(command.allowed_hosts, (std::vector<std::string>{"example.org"}));
    EXPECT_EQ(command.options.max_pages, 8U);
    EXPECT_EQ(command.options.max_targets, 24U);
    EXPECT_EQ(command.options.frontier.max_depth, 0U);
    EXPECT_EQ(command.options.frontier.max_pending, 100U);
    EXPECT_EQ(command.options.frontier.max_urls, 110U);
    EXPECT_EQ(command.options.max_duration, std::chrono::milliseconds{60000});
    EXPECT_EQ(command.options.timing.interval, std::chrono::milliseconds{0});
    EXPECT_EQ(command.options.language_policy, hfs::CrawlLanguagePolicy::any);
    EXPECT_FALSE(command.output_path);
    EXPECT_FALSE(command.log_path);
}

TEST(Config, PreservesDefaultsForOmittedOptionsAndAcceptsUtf8Bom)
{
    for (const auto& input : {std::string{minimal}, std::string{"\xEF\xBB\xBF"} + std::string{minimal}}) {
        const auto parsed = hfs::parse_crawl_config(input);
        ASSERT_TRUE(std::holds_alternative<hfs::CrawlCommand>(parsed));
        const auto& options = std::get<hfs::CrawlCommand>(parsed).options;
        const hfs::CrawlOptions defaults;
        EXPECT_EQ(options.max_pages, defaults.max_pages);
        EXPECT_EQ(options.max_targets, defaults.max_targets);
        EXPECT_EQ(options.max_duration, defaults.max_duration);
        EXPECT_EQ(options.language_policy, hfs::CrawlLanguagePolicy::english_only);
    }
}

TEST(Config, RejectsAmbiguousMalformedUnknownAndNestedStructures)
{
    for (const auto* input : {"", "{}", "[]", "null", "1", "{broken}",
        R"({"schema_version":true,"seeds":["https://example.org/"],"allowed_hosts":["example.org"]})",
        R"({"schema_version":1.0,"seeds":["https://example.org/"],"allowed_hosts":["example.org"]})",
        R"({"schema_version":2,"seeds":["https://example.org/"],"allowed_hosts":["example.org"]})",
        R"({"schema_version":1,"schema_version":1,"seeds":["https://example.org/"],"allowed_hosts":["example.org"]})",
        R"({"schema_version":1,"seeds":["https://example.org/"],"\u0073eeds":["https://example.org/"],"allowed_hosts":["example.org"]})",
        R"({"schema_version":1,"seeds":[["https://example.org/"]],"allowed_hosts":[]})",
        R"({"schema_version":1,"seeds":{},"allowed_hosts":[]})",
        R"({"schema_version":1,"seeds":[true],"allowed_hosts":[]})",
        R"({"schema_version":1,"seeds":["https://example.org/"]})",
        R"({"schema_version":1,"seeds":["https://example.org/"],"allowed_hosts":["example.org"],"output":"data.jsonl"})",
        R"({"schema_version":1,"seeds":["https://example.org/"],"allowed_hosts":["example.org"],"manifest":"run.json"})",
        R"({"schema_version":1,"seeds":["https://example.org/"],"allowed_hosts":["example.org"],"max_pgaes":3})"}) {
        SCOPED_TRACE(input);
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(hfs::parse_crawl_config(input)));
    }
    EXPECT_TRUE(std::holds_alternative<hfs::CliError>(hfs::parse_crawl_config(std::string{minimal} + " trailing")));
}

TEST(Config, ReusesCliScopeAndNumericValidationWithoutCoercion)
{
    for (const auto* number : {"-1", "0", "true", "1.5", "\"2\"", "18446744073709551616", "9e99"}) {
        std::string input{minimal};
        input.pop_back();
        input += ",\"max_pages\":" + std::string{number} + "}";
        SCOPED_TRACE(number);
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(hfs::parse_crawl_config(input)));
    }
    for (const auto* input : {
        R"({"schema_version":1,"seeds":["https://outside.invalid/"],"allowed_hosts":["example.org"]})",
        R"({"schema_version":1,"seeds":["https://user:pass@example.org/"],"allowed_hosts":["example.org"]})",
        R"({"schema_version":1,"seeds":["https://example.org/"],"allowed_hosts":["https://example.org"]})",
        R"({"schema_version":1,"seeds":["https://example.org/"],"allowed_hosts":["example.org"],"language":"de"})",
        R"({"schema_version":1,"seeds":[],"allowed_hosts":["example.org"]})"}) {
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(hfs::parse_crawl_config(input)));
    }
}

TEST(Config, EnforcesTheFileSizeCapBeforeParsing)
{
    std::string input{minimal};
    input.resize(hfs::crawl_config_max_bytes, ' ');
    EXPECT_TRUE(std::holds_alternative<hfs::CrawlCommand>(hfs::parse_crawl_config(input)));
    input.push_back(' ');
    EXPECT_TRUE(std::holds_alternative<hfs::CliError>(hfs::parse_crawl_config(input)));
}

TEST(Config, LoadsTheCommittedPilotThroughTheBoundedFileReader)
{
    EXPECT_TRUE(std::holds_alternative<hfs::CliError>(hfs::load_crawl_config({})));
    EXPECT_TRUE(std::holds_alternative<hfs::CliError>(hfs::load_crawl_config(std::string_view{"bad\0path", 8})));
    const auto parsed = hfs::load_crawl_config(HFS_PILOT_CONFIG);
    ASSERT_TRUE(std::holds_alternative<hfs::CrawlCommand>(parsed));
    const auto& command = std::get<hfs::CrawlCommand>(parsed);
    EXPECT_EQ(command.seeds.size(), 2U);
    EXPECT_EQ(command.seeds[0], "https://www.python.org/about/");
    EXPECT_EQ(command.allowed_hosts, (std::vector<std::string>{"www.python.org", "www.rfc-editor.org"}));
    EXPECT_EQ(command.options.max_pages, 8U);
    EXPECT_EQ(command.options.frontier.max_urls, 100U);
}
