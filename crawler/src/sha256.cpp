#include "sha256.hpp"
#include "hfs/file_fingerprint.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace {

#ifdef _WIN32
void close_algorithm(void* handle) noexcept { (void)BCryptCloseAlgorithmProvider(handle, 0); }
void destroy_hash(void* handle) noexcept { (void)BCryptDestroyHash(handle); }
#endif

class Sha256 {
public:
    Sha256()
    {
#ifdef _WIN32
        BCRYPT_ALG_HANDLE algorithm{};
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
            throw std::runtime_error{"Could not open the SHA-256 provider"};
        }
        algorithm_.reset(algorithm);
        BCRYPT_HASH_HANDLE hash{};
        // CNG owns the hash object's buffer until the handle is destroyed.
        if (BCryptCreateHash(algorithm_.get(), &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            throw std::runtime_error{"Could not create a SHA-256 hash"};
        }
        hash_.reset(hash);
#else
        if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error{"Could not create a SHA-256 hash"};
        }
#endif
    }

    void update(std::string_view input)
    {
#ifdef _WIN32
        while (!input.empty()) {
            const auto length = std::min(input.size(), static_cast<std::size_t>(std::numeric_limits<ULONG>::max()));
            // BCrypt reads the bytes but its API omits const qualification.
            auto* bytes = reinterpret_cast<PUCHAR>(const_cast<char*>(input.data()));
            if (BCryptHashData(hash_.get(), bytes, static_cast<ULONG>(length), 0) < 0) {
                throw std::runtime_error{"Could not update a SHA-256 hash"};
            }
            input.remove_prefix(length);
        }
#else
        if (!input.empty() && EVP_DigestUpdate(context_.get(), input.data(), input.size()) != 1) {
            throw std::runtime_error{"Could not update a SHA-256 hash"};
        }
#endif
    }

    auto finish() -> std::string
    {
        std::array<unsigned char, 32> digest{};
#ifdef _WIN32
        if (BCryptFinishHash(hash_.get(), digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
            throw std::runtime_error{"Could not finish a SHA-256 hash"};
        }
#else
        unsigned int length{};
        if (EVP_DigestFinal_ex(context_.get(), digest.data(), &length) != 1 || length != digest.size()) {
            throw std::runtime_error{"Could not finish a SHA-256 hash"};
        }
#endif
        constexpr std::string_view hex{"0123456789abcdef"};
        std::string result{"sha256:"};
        result.reserve(result.size() + digest.size() * 2);
        for (const auto byte : digest) {
            result += hex[byte >> 4];
            result += hex[byte & 0x0F];
        }
        return result;
    }

private:
#ifdef _WIN32
    // Members are destroyed in reverse order: hash first, provider afterwards.
    std::unique_ptr<void, decltype(&close_algorithm)> algorithm_{nullptr, close_algorithm};
    std::unique_ptr<void, decltype(&destroy_hash)> hash_{nullptr, destroy_hash};
#else
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(), EVP_MD_CTX_free};
#endif
};

} // namespace

namespace hfs::detail {

auto sha256(std::string_view input) -> std::string
{
    Sha256 hash;
    hash.update(input);
    return hash.finish();
}

} // namespace hfs::detail

namespace hfs {

auto fingerprint_file(const std::filesystem::path& path) -> FileFingerprint
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"Could not open an artifact for SHA-256"};
    }
    Sha256 hash;
    std::uintmax_t bytes{};
    std::array<char, 64 * 1024> buffer{};
    while (stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || stream.gcount() > 0) {
        const auto size = static_cast<std::size_t>(stream.gcount());
        if (size > std::numeric_limits<std::uintmax_t>::max() - bytes) {
            throw std::runtime_error{"Artifact byte count overflow"};
        }
        hash.update(std::string_view{buffer.data(), size});
        bytes += size;
    }
    if (!stream.eof() || stream.bad()) {
        throw std::runtime_error{"Could not read an artifact for SHA-256"};
    }
    return FileFingerprint{bytes, hash.finish()};
}

} // namespace hfs
