#include "hfs/jsonl.hpp"
#include "hfs/crawl.hpp"
#include "hfs/page_identity.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <utility>

namespace {
auto sample_page() -> hfs::CrawledPage
{
    hfs::CrawledPage page;
    page.url = "https://example.org/article";
    page.title = "A title";
    page.text = "First line\nSecond line";
    page.depth = 2;
    page.http_status = 200;
    page.content_type = "text/html; charset=utf-8";
    page.requested_urls = {"https://example.org/alias", page.url};
    using namespace std::chrono;
    page.fetched_at = sys_days{2026y/November/9} + 12h + 34min + 56s + 999ms;
    const auto identity = hfs::make_page_identity(page.url, page.text).value();
    page.page_id = identity.page_id;
    page.text_hash = identity.text_hash;
    return page;
}
}

TEST(Jsonl, SerializesPageFieldsCapturedTimeAndUnknownLanguage)
{
    const auto page = sample_page();
    std::ostringstream output;
    hfs::write_page_jsonl(output, page);
    const auto record = nlohmann::json::parse(output.str());
    EXPECT_EQ(record.at("schema_version"), 1);
    EXPECT_EQ(record.at("extractor_version"), "semantic-text-v2");
    EXPECT_EQ(record.at("text_source"), "body");
    EXPECT_EQ(record.at("url_normalization_version"), "lexbor-url-v1");
    EXPECT_EQ(record.at("url"), page.url);
    EXPECT_EQ(record.at("page_id"), page.page_id);
    EXPECT_EQ(record.at("text_hash"), page.text_hash);
    EXPECT_EQ(record.at("title"), page.title);
    EXPECT_EQ(record.at("text"), page.text);
    EXPECT_EQ(record.at("depth"), 2);
    EXPECT_EQ(record.at("http_status"), 200);
    EXPECT_EQ(record.at("content_type"), page.content_type);
    EXPECT_EQ(record.at("requested_urls"), page.requested_urls);
    EXPECT_TRUE(record.at("language").is_null());
    EXPECT_TRUE(record.at("language_percent").is_null());
    EXPECT_TRUE(record.at("language_detection_version").is_null());
    EXPECT_EQ(record.at("fetched_at"), "2026-11-09T12:34:56Z");
}

TEST(Jsonl, SerializesMainRoleSourceWithUpdatedExtractorVersion)
{
    auto page = sample_page();
    page.text_source = hfs::HtmlTextSource::main_role;
    std::ostringstream output;
    hfs::write_page_jsonl(output, page);
    const auto record = nlohmann::json::parse(output.str());
    EXPECT_EQ(record.at("text_source"), "main_role");
    EXPECT_EQ(record.at("extractor_version"), "semantic-text-v2");
}

TEST(Jsonl, PreservesDetectedAndUnknownLanguageMetadata)
{
    auto page = sample_page();
    page.language = hfs::LanguageResult{hfs::LanguageStatus::detected, "en", 97};
    std::ostringstream known;
    hfs::write_page_jsonl(known, page);
    const auto record = nlohmann::json::parse(known.str());
    EXPECT_EQ(record.at("language"), "en");
    EXPECT_EQ(record.at("language_percent"), 97);
    EXPECT_EQ(record.at("language_detection_version"), "cld2-compact-v1");
    page.language = hfs::LanguageResult{hfs::LanguageStatus::too_short, std::nullopt, 0};
    std::ostringstream unknown;
    hfs::write_page_jsonl(unknown, page);
    const auto absent = nlohmann::json::parse(unknown.str());
    EXPECT_TRUE(absent.at("language").is_null());
    EXPECT_TRUE(absent.at("language_percent").is_null());
    EXPECT_EQ(absent.at("language_detection_version"), "cld2-compact-v1");
}

TEST(Jsonl, FormatsUtcAcrossEpochAndLeapDayBoundaries)
{
    using namespace std::chrono;
    const std::pair<system_clock::time_point, std::string_view> cases[]{
        {system_clock::time_point{}, "1970-01-01T00:00:00Z"},
        {system_clock::time_point{} - 1ms, "1969-12-31T23:59:59Z"},
        {sys_days{2024y/February/29} + 23h + 59min + 59s + 999ms, "2024-02-29T23:59:59Z"},
        {sys_days{2024y/March/1}, "2024-03-01T00:00:00Z"}};
    for (const auto& [time, expected] : cases) {
        auto page = sample_page();
        page.fetched_at = time;
        std::ostringstream output;
        hfs::write_page_jsonl(output, page);
        EXPECT_EQ(nlohmann::json::parse(output.str()).at("fetched_at"), expected);
    }
}

TEST(Jsonl, KeepsMissingCaptureTimeExplicitlyUnknown)
{
    auto page = sample_page();
    page.fetched_at.reset();
    std::ostringstream output;
    hfs::write_page_jsonl(output, page);
    EXPECT_TRUE(nlohmann::json::parse(output.str()).at("fetched_at").is_null());
}

TEST(Jsonl, EscapesControlsAndQuotesInOneUtf8Line)
{
    auto page = sample_page();
    page.title = "Quoted \"title\" \\ Caf\xC3\xA9";
    page.text = std::string{"Line\nTab\tReturn\rNul\0End", 23};
    std::ostringstream output;
    hfs::write_page_jsonl(output, page);
    const auto line = output.str();
    EXPECT_EQ(std::count(line.begin(), line.end(), '\n'), 1);
    EXPECT_EQ(std::count(line.begin(), line.end(), '\r'), 0);
    EXPECT_EQ(line.back(), '\n');
    EXPECT_NE(line.find("Caf\xC3\xA9"), std::string::npos);
    const auto record = nlohmann::json::parse(line);
    EXPECT_EQ(record.at("title"), page.title);
    EXPECT_EQ(record.at("text"), page.text);
}

TEST(Jsonl, ProducesIdenticalBytesForRepeatedRecords)
{
    const auto page = sample_page();
    std::ostringstream first;
    hfs::write_page_jsonl(first, page);
    std::ostringstream second;
    hfs::write_page_jsonl(second, page);
    hfs::write_page_jsonl(second, page);
    EXPECT_EQ(second.str(), first.str() + first.str());
}

TEST(Jsonl, RejectsInvalidUtf8BeforeWritingAnyPartOfTheRecord)
{
    auto page = sample_page();
    page.title = "Broken \xFF";
    std::ostringstream output;
    output << "previous record\n";
    EXPECT_THROW(hfs::write_page_jsonl(output, page), nlohmann::json::type_error);
    EXPECT_EQ(output.str(), "previous record\n");
}

TEST(Jsonl, PropagatesStreamFailures)
{
    const auto page = sample_page();
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    EXPECT_THROW(hfs::write_page_jsonl(failed, page), std::runtime_error);

    class ShortBuffer : public std::streambuf {
        auto xsputn(const char*, std::streamsize count) -> std::streamsize override
        {
            return count > 0 ? count - 1 : 0;
        }
    } buffer;
    std::ostream partial{&buffer};
    EXPECT_THROW(hfs::write_page_jsonl(partial, page), std::runtime_error);
}
