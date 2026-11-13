#pragma once

#include "hfs/html_input.hpp"
#include "hfs/http_fetch.hpp"
#include "hfs/language.hpp"
#include "hfs/url_frontier.hpp"

#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace hfs {

enum class CrawlOutcome {
    page_delivered, fetch_failed, response_skipped, empty_text, duplicate_url, duplicate_text,
    non_english, unknown_language
};

// One completed queue target, not an individual HTTP or robots request.
// Values own their data so a caller can retain a copy after the callback.
struct CrawlEvent {
    CrawlTarget target;
    CrawlOutcome outcome{CrawlOutcome::page_delivered};
    // Absent on fetch failure: the fetcher does not expose a partial request chain.
    std::optional<std::string> final_url;
    // Absent on fetch failure or a queued URL skipped without a new fetch.
    std::optional<long> http_status;
    // Checked only for status 200, before extraction.
    std::optional<HtmlInputStatus> html_input;
    std::variant<std::monostate, HttpError, FetchError> fetch_error;
    // Present after analyzing nonempty extracted text, including language rejections.
    std::optional<LanguageResult> language;
};

// Borrowed for this call only. Empty observers disable events; exceptions propagate.
using CrawlObserver = std::function<void(const CrawlEvent&)>;

} // namespace hfs
