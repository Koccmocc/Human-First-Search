#pragma once

#include "hfs/cli.hpp"

#include <chrono>
#include <iosfwd>

namespace hfs {

struct RunTiming {
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point finished_at;
    std::chrono::milliseconds elapsed{};
};

// Call after a normal crawl return and after closing corpus/journal files.
// Reads artifact bytes, then serializes before writing. Errors throw; caller closes.
void write_run_manifest(std::ostream& output, const CrawlCommand& command,
                        const CrawlSummary& summary, const RunTiming& timing);

} // namespace hfs
