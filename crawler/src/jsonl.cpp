#include "hfs/jsonl.hpp"
#include "hfs/crawl.hpp"

#include <nlohmann/json.hpp>

#include <ostream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace {

auto utc_timestamp(std::chrono::system_clock::time_point time) -> std::string
{
    using namespace std::chrono;
    // floor(), rather than truncation, also handles instants before the Unix epoch.
    const auto whole_seconds = floor<seconds>(time);
    const auto date = floor<days>(whole_seconds);
    if (date < sys_days{year{0}/January/1} || date >= sys_days{year{10000}/January/1}) {
        throw std::out_of_range{"Fetch timestamp must have a four-digit UTC year"};
    }
    const year_month_day calendar{date};
    const hh_mm_ss time_of_day{whole_seconds - date};
    std::ostringstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);
    // Numeric formatting must not inherit the machine's locale or time zone.
    output.imbue(std::locale::classic());
    output << std::setfill('0') << std::setw(4) << static_cast<int>(calendar.year())
           << '-' << std::setw(2) << static_cast<unsigned>(calendar.month())
           << '-' << std::setw(2) << static_cast<unsigned>(calendar.day())
           << 'T' << std::setw(2) << time_of_day.hours().count()
           << ':' << std::setw(2) << time_of_day.minutes().count()
           << ':' << std::setw(2) << time_of_day.seconds().count() << 'Z';
    return output.str();
}

} // namespace

namespace hfs {

void write_page_jsonl(std::ostream& output, const CrawledPage& page)
{
    const nlohmann::json record{
        {"schema_version", 1},
        {"extractor_version", "body-text-v1"},
        {"url_normalization_version", "lexbor-url-v1"},
        {"page_id", page.page_id},
        {"url", page.url},
        {"title", page.title},
        {"text", page.text},
        // Language detection is still pending; a manually supplied page may lack time.
        {"language", nullptr},
        {"fetched_at", page.fetched_at ? nlohmann::json(utc_timestamp(*page.fetched_at))
                                       : nlohmann::json(nullptr)},
        {"text_hash", page.text_hash},
        {"http_status", page.http_status},
        {"content_type", page.content_type},
        {"depth", page.depth},
        {"requested_urls", page.requested_urls}};

    // dump() validates UTF-8 and escapes controls before touching the output stream.
    auto line = record.dump();
    line.push_back('\n');
    if (!(output << line)) {
        throw std::runtime_error{"Could not write JSONL record"};
    }
}

} // namespace hfs
