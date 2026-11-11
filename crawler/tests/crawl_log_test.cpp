#include "hfs/crawl_log.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <utility>

TEST(CrawlLog, SerializesKnownFieldsAndAbsentMetadataAsOneLine)
{
    hfs::CrawlEvent event;
    event.target = {"https://example.org/alias", 2};
    event.final_url = "https://example.org/page";
    event.http_status = 200;
    event.html_input = hfs::HtmlInputStatus::ready;
    std::ostringstream output;
    hfs::write_crawl_event_jsonl(output, event);
    const auto line = output.str();
    EXPECT_EQ(std::count(line.begin(), line.end(), '\n'), 1);
    EXPECT_EQ(line.back(), '\n');
    const auto record = nlohmann::json::parse(line);
    EXPECT_EQ(record.at("schema_version"), 1);
    EXPECT_EQ(record.at("event"), "crawl_target");
    EXPECT_EQ(record.at("url"), event.target.url);
    EXPECT_EQ(record.at("depth"), 2);
    EXPECT_EQ(record.at("outcome"), "page_delivered");
    EXPECT_EQ(record.at("final_url"), *event.final_url);
    EXPECT_EQ(record.at("http_status"), 200);
    EXPECT_EQ(record.at("html_input"), "ready");
    EXPECT_TRUE(record.at("error").is_null());

    event.final_url.reset();
    event.http_status.reset();
    event.html_input.reset();
    std::ostringstream absent;
    hfs::write_crawl_event_jsonl(absent, event);
    const auto missing = nlohmann::json::parse(absent.str());
    for (const auto* field : {"final_url", "http_status", "html_input", "error"}) {
        EXPECT_TRUE(missing.at(field).is_null());
    }
}

TEST(CrawlLog, PreservesOutcomeAndInputNames)
{
    const std::pair<hfs::CrawlOutcome, std::string_view> outcomes[]{
        {hfs::CrawlOutcome::fetch_failed, "fetch_failed"},
        {hfs::CrawlOutcome::response_skipped, "response_skipped"},
        {hfs::CrawlOutcome::empty_text, "empty_text"},
        {hfs::CrawlOutcome::duplicate_url, "duplicate_url"},
        {hfs::CrawlOutcome::duplicate_text, "duplicate_text"}};
    for (const auto& [value, name] : outcomes) {
        hfs::CrawlEvent event;
        event.outcome = value;
        std::ostringstream output;
        hfs::write_crawl_event_jsonl(output, event);
        EXPECT_EQ(nlohmann::json::parse(output.str()).at("outcome"), name);
    }
    const std::pair<hfs::HtmlInputStatus, std::string_view> inputs[]{
        {hfs::HtmlInputStatus::not_html, "not_html"},
        {hfs::HtmlInputStatus::invalid_content_type, "invalid_content_type"},
        {hfs::HtmlInputStatus::unsupported_encoding, "unsupported_encoding"},
        {hfs::HtmlInputStatus::invalid_text, "invalid_text"}};
    for (const auto& [value, name] : inputs) {
        hfs::CrawlEvent event;
        event.html_input = value;
        std::ostringstream output;
        hfs::write_crawl_event_jsonl(output, event);
        EXPECT_EQ(nlohmann::json::parse(output.str()).at("html_input"), name);
    }
}

TEST(CrawlLog, DistinguishesErrorCategoriesAndEscapesMessages)
{
    hfs::CrawlEvent event;
    event.outcome = hfs::CrawlOutcome::fetch_failed;
    const std::string message = "Quoted \"error\" \\ Caf\xC3\xA9\nNext\tline";
    const std::pair<hfs::HttpErrorKind, std::string_view> http_errors[]{
        {hfs::HttpErrorKind::invalid_request, "invalid_request"},
        {hfs::HttpErrorKind::timeout, "timeout"},
        {hfs::HttpErrorKind::body_limit, "body_limit"},
        {hfs::HttpErrorKind::transport, "transport"}};
    for (const auto& [kind, name] : http_errors) {
        event.fetch_error = hfs::HttpError{kind, message};
        std::ostringstream output;
        hfs::write_crawl_event_jsonl(output, event);
        const auto line = output.str();
        EXPECT_EQ(std::count(line.begin(), line.end(), '\n'), 1);
        const auto error = nlohmann::json::parse(line).at("error");
        EXPECT_EQ(error.at("category"), "http");
        EXPECT_EQ(error.at("kind"), name);
        EXPECT_EQ(error.at("message"), message);
    }
    const std::pair<hfs::FetchErrorKind, std::string_view> fetch_errors[]{
        {hfs::FetchErrorKind::invalid_url, "invalid_url"},
        {hfs::FetchErrorKind::request_denied, "request_denied"},
        {hfs::FetchErrorKind::redirect_loop, "redirect_loop"},
        {hfs::FetchErrorKind::redirect_limit, "redirect_limit"}};
    for (const auto& [kind, name] : fetch_errors) {
        event.fetch_error = hfs::FetchError{kind, message};
        std::ostringstream output;
        hfs::write_crawl_event_jsonl(output, event);
        const auto error = nlohmann::json::parse(output.str()).at("error");
        EXPECT_EQ(error.at("category"), "fetch");
        EXPECT_EQ(error.at("kind"), name);
        EXPECT_EQ(error.at("message"), message);
    }
}

TEST(CrawlLog, ValidatesBeforeWritingAndIsRepeatable)
{
    hfs::CrawlEvent event;
    std::ostringstream first;
    std::ostringstream second;
    hfs::write_crawl_event_jsonl(first, event);
    hfs::write_crawl_event_jsonl(second, event);
    EXPECT_EQ(first.str(), second.str());
    event.target.url = "Broken \xFF";
    EXPECT_THROW(hfs::write_crawl_event_jsonl(second, event), nlohmann::json::type_error);
    EXPECT_EQ(second.str(), first.str());
    event.target.url.clear();
    event.outcome = static_cast<hfs::CrawlOutcome>(-1);
    EXPECT_THROW(hfs::write_crawl_event_jsonl(second, event), std::invalid_argument);
    EXPECT_EQ(second.str(), first.str());
}

TEST(CrawlLog, PropagatesFailedAndPartialWrites)
{
    const hfs::CrawlEvent event;
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    EXPECT_THROW(hfs::write_crawl_event_jsonl(failed, event), std::runtime_error);
    class ShortBuffer : public std::streambuf {
        auto xsputn(const char*, std::streamsize count) -> std::streamsize override
        {
            return count > 0 ? count - 1 : 0;
        }
    } buffer;
    std::ostream partial{&buffer};
    EXPECT_THROW(hfs::write_crawl_event_jsonl(partial, event), std::runtime_error);
}
