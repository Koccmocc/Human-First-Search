#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace hfs {

struct FileFingerprint {
    std::uintmax_t bytes{};
    std::string sha256;
};

// Hash exact binary bytes with bounded memory. Read/provider failures throw.
[[nodiscard]] auto fingerprint_file(const std::filesystem::path& path) -> FileFingerprint;

} // namespace hfs
