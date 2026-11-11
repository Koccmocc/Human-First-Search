#pragma once

#include "hfs/crawl_event.hpp"

#include <iosfwd>

namespace hfs {

// Write one owning event as UTF-8 JSONL. The caller owns, flushes and closes the stream.
// Serialization and output failures propagate; no event timestamp is invented.
void write_crawl_event_jsonl(std::ostream& output, const CrawlEvent& event);

} // namespace hfs
