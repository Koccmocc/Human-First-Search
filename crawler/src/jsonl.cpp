#include "hfs/jsonl.hpp"
#include "hfs/crawl.hpp"

#include <nlohmann/json.hpp>

#include <ostream>
#include <stdexcept>

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
        // Language detection and fetch timestamps have not been captured yet.
        {"language", nullptr},
        {"fetched_at", nullptr},
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
