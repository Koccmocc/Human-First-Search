#include "hfs/crawl_log.hpp"

#include <nlohmann/json.hpp>

#include <ostream>
#include <stdexcept>
#include <string_view>

namespace {

auto outcome_name(hfs::CrawlOutcome value) -> std::string_view
{
    using enum hfs::CrawlOutcome;
    switch (value) {
    case page_delivered: return "page_delivered";
    case fetch_failed: return "fetch_failed";
    case response_skipped: return "response_skipped";
    case empty_text: return "empty_text";
    case duplicate_url: return "duplicate_url";
    case duplicate_text: return "duplicate_text";
    }
    throw std::invalid_argument{"Unknown crawl outcome"};
}

auto input_name(hfs::HtmlInputStatus value) -> std::string_view
{
    using enum hfs::HtmlInputStatus;
    switch (value) {
    case ready: return "ready";
    case not_html: return "not_html";
    case invalid_content_type: return "invalid_content_type";
    case unsupported_encoding: return "unsupported_encoding";
    case invalid_text: return "invalid_text";
    }
    throw std::invalid_argument{"Unknown HTML input status"};
}

auto error_name(hfs::HttpErrorKind value) -> std::string_view
{
    using enum hfs::HttpErrorKind;
    switch (value) {
    case invalid_request: return "invalid_request";
    case timeout: return "timeout";
    case body_limit: return "body_limit";
    case transport: return "transport";
    }
    throw std::invalid_argument{"Unknown HTTP error kind"};
}

auto error_name(hfs::FetchErrorKind value) -> std::string_view
{
    using enum hfs::FetchErrorKind;
    switch (value) {
    case invalid_url: return "invalid_url";
    case request_denied: return "request_denied";
    case redirect_loop: return "redirect_loop";
    case redirect_limit: return "redirect_limit";
    }
    throw std::invalid_argument{"Unknown fetch error kind"};
}

} // namespace

namespace hfs {

void write_crawl_event_jsonl(std::ostream& output, const CrawlEvent& event)
{
    nlohmann::json error = nullptr;
    if (const auto* http = std::get_if<HttpError>(&event.fetch_error)) {
        error = {{"category", "http"}, {"kind", error_name(http->kind)}, {"message", http->message}};
    } else if (const auto* fetch = std::get_if<FetchError>(&event.fetch_error)) {
        error = {{"category", "fetch"}, {"kind", error_name(fetch->kind)}, {"message", fetch->message}};
    }
    const nlohmann::json record{
        {"schema_version", 1},
        {"event", "crawl_target"},
        {"url", event.target.url},
        {"depth", event.target.depth},
        {"outcome", outcome_name(event.outcome)},
        {"final_url", event.final_url ? nlohmann::json(*event.final_url) : nlohmann::json(nullptr)},
        {"http_status", event.http_status ? nlohmann::json(*event.http_status) : nlohmann::json(nullptr)},
        {"html_input", event.html_input ? nlohmann::json(input_name(*event.html_input))
                                       : nlohmann::json(nullptr)},
        {"error", error}};
    // Validate and escape the whole record before modifying the stream.
    auto line = record.dump();
    line.push_back('\n');
    if (!(output << line)) {
        throw std::runtime_error{"Could not write crawl log JSONL record"};
    }
}

} // namespace hfs
