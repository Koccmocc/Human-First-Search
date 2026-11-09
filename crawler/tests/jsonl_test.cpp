#include "hfs/jsonl.hpp"
#include "hfs/crawl.hpp"
#include "hfs/page_identity.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <streambuf>

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
    const auto identity = hfs::make_page_identity(page.url, page.text).value();
    page.page_id = identity.page_id;
    page.text_hash = identity.text_hash;
    return page;
}
}

TEST(Jsonl, SerializesPageFieldsAndExplicitUnknownMetadata)
{
    const auto page = sample_page();
    std::ostringstream output;
    hfs::write_page_jsonl(output, page);
    const auto record = nlohmann::json::parse(output.str());
    EXPECT_EQ(record.at("schema_version"), 1);
    EXPECT_EQ(record.at("extractor_version"), "body-text-v1");
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
    EXPECT_TRUE(record.at("fetched_at").is_null());
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
