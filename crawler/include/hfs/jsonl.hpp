#pragma once

#include <iosfwd>

namespace hfs {

struct CrawledPage;

// Writes one compact UTF-8 JSON object followed by LF. The caller owns the stream
// and must flush/close it. Serialization and output failures propagate as exceptions.
void write_page_jsonl(std::ostream& output, const CrawledPage& page);

} // namespace hfs
