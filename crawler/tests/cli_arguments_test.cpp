#include "hfs/cli.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>
#include <utility>

namespace {
auto parse(std::initializer_list<std::string_view> arguments) -> hfs::CliResult
{
    return hfs::parse_arguments({arguments.begin(), arguments.size()});
}
}

TEST(CliArguments, PreservesHelpVersionAndRejectsMixedActions)
{
    EXPECT_EQ(std::get<hfs::CliAction>(parse({})), hfs::CliAction::help);
    EXPECT_EQ(std::get<hfs::CliAction>(parse({"crawl", "--help"})), hfs::CliAction::help);
    EXPECT_EQ(std::get<hfs::CliAction>(parse({"--version"})), hfs::CliAction::version);
    EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"--help", "crawl"})));
    EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"--unknown"})));
}

TEST(CliArguments, BuildsOwningCanonicalConfigurationWithMultipleSeedsAndHosts)
{
    auto result = parse({"crawl", "--seed", "HTTPS://EXAMPLE.org:443/start#part",
                         "--seed", "http://[::1]/", "--allow-host", "EXAMPLE.org",
                         "--allow-host", "[::1]", "--allow-host", "example.org",
                         "--max-pages", "12", "--max-targets", "30", "--max-depth", "2",
                         "--max-pending", "20", "--max-urls", "40", "--delay-ms", "250"});
    const auto* command = std::get_if<hfs::CrawlCommand>(&result);
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->seeds, (std::vector<std::string>{"https://example.org/start", "http://[::1]/"}));
    EXPECT_EQ(command->allowed_hosts, (std::vector<std::string>{"example.org", "[::1]"}));
    EXPECT_EQ(command->options.max_pages, 12U);
    EXPECT_EQ(command->options.max_targets, 30U);
    EXPECT_EQ(command->options.frontier.max_depth, 2U);
    EXPECT_EQ(command->options.frontier.max_pending, 20U);
    EXPECT_EQ(command->options.frontier.max_urls, 40U);
    EXPECT_EQ(command->options.timing.interval, std::chrono::milliseconds{250});

    std::string seed{"https://example.org/owned"};
    const auto owned = parse({"crawl", "--seed", seed, "--allow-host", "example.org"});
    seed.assign("changed");
    EXPECT_EQ(std::get<hfs::CrawlCommand>(owned).seeds.front(), "https://example.org/owned");
}

TEST(CliArguments, AcceptsOneOwningOutputPathAndRejectsMissingEmptyOrRepeatedPaths)
{
    std::string path{"data/corpus with spaces.jsonl"};
    const auto result = parse({"crawl", "--seed", "https://example.org/", "--allow-host", "example.org",
                               "--output", path});
    path.assign("changed");
    ASSERT_TRUE(std::holds_alternative<hfs::CrawlCommand>(result));
    EXPECT_EQ(std::get<hfs::CrawlCommand>(result).output_path, "data/corpus with spaces.jsonl");
    for (const auto& invalid : {parse({"crawl", "--output"}), parse({"crawl", "--output", ""}),
                                parse({"crawl", "--output", "one", "--output", "two"})}) {
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(invalid));
    }
}

TEST(CliArguments, KeepsCrawlDefaultsAndAllowsZeroDepthAndDelay)
{
    const auto defaults = parse({"crawl", "--allow-host", "example.org", "--seed", "https://example.org/"});
    ASSERT_TRUE(std::holds_alternative<hfs::CrawlCommand>(defaults));
    const auto& options = std::get<hfs::CrawlCommand>(defaults).options;
    const hfs::CrawlOptions expected;
    EXPECT_EQ(options.max_pages, expected.max_pages);
    EXPECT_EQ(options.timing.interval, expected.timing.interval);
    const auto zero = parse({"crawl", "--seed", "https://example.org/", "--allow-host", "example.org",
                             "--max-depth", "0", "--delay-ms", "0"});
    ASSERT_TRUE(std::holds_alternative<hfs::CrawlCommand>(zero));
    EXPECT_EQ(std::get<hfs::CrawlCommand>(zero).options.frontier.max_depth, 0U);
    EXPECT_EQ(std::get<hfs::CrawlCommand>(zero).options.timing.interval.count(), 0);
}

TEST(CliArguments, RequiresExplicitSeedsAndScopeAndChecksEverySeed)
{
    for (const auto& result : {parse({"crawl"}), parse({"crawl", "--seed", "https://example.org/"}),
                              parse({"crawl", "--allow-host", "example.org"}),
                              parse({"crawl", "--seed", "https://sub.example.org/", "--allow-host", "example.org"}),
                              parse({"crawl", "--seed", "https://example.org/", "--seed", "https://outside.invalid/",
                                     "--allow-host", "example.org"})}) {
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(result));
    }
}

TEST(CliArguments, RejectsUrlsPortsCredentialsAndControlCharactersAsHosts)
{
    for (const auto* host : {"", "https://example.org", "example.org/path", "example.org:80",
                             "user@example.org", "example.org?x=1", "example.org#part", "[::1]:80",
                             "example.org\\path", "example.org\n", " example.org"}) {
        SCOPED_TRACE(host);
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"crawl", "--allow-host", host,
                                                                 "--seed", "https://example.org/"})));
    }
    for (const auto* seed : {"relative", "file:///unused", "https://user:pass@example.org/", ""}) {
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"crawl", "--allow-host", "example.org",
                                                                 "--seed", seed})));
    }
}

TEST(CliArguments, RejectsMalformedOverflowingOrRepeatedNumericOptions)
{
    for (const auto* option : {"--max-pages", "--max-targets", "--max-depth", "--max-pending", "--max-urls", "--delay-ms"}) {
        for (const auto* value : {"", "-1", "+1", " 1", "1 ", "1.5", "1x", "18446744073709551616"}) {
            SCOPED_TRACE(std::string{option} + " " + value);
            EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"crawl", "--seed", "https://example.org/",
                "--allow-host", "example.org", option, value})));
        }
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"crawl", "--seed", "https://example.org/",
            "--allow-host", "example.org", option, "1", option, "2"})));
    }
    for (const auto* option : {"--max-pages", "--max-targets", "--max-pending", "--max-urls"}) {
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"crawl", "--seed", "https://example.org/",
            "--allow-host", "example.org", option, "0"})));
    }
    // A size_t value can exceed the signed representation used by milliseconds.
    if (!std::in_range<std::chrono::milliseconds::rep>(std::numeric_limits<std::size_t>::max())) {
        const auto too_large = std::to_string(std::numeric_limits<std::size_t>::max());
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(parse({"crawl", "--seed", "https://example.org/",
            "--allow-host", "example.org", "--delay-ms", too_large})));
    }
}

TEST(CliArguments, RejectsMissingValuesUnknownFlagsAndPositionalArguments)
{
    for (const auto& result : {parse({"crawl", "--seed"}), parse({"crawl", "--seed", "--allow-host"}),
                              parse({"crawl", "--max-pages"}), parse({"crawl", "--unknown", "1"}),
                              parse({"crawl", "https://example.org/"}), parse({"crawl", "--max-pages=1"})}) {
        EXPECT_TRUE(std::holds_alternative<hfs::CliError>(result));
    }
}
