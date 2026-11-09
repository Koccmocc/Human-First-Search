#include "hfs/page_identity.hpp"
#include "hfs/url.hpp"

#include <array>
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

#include <algorithm>
#include <limits>
#include <memory>
#else
#include <openssl/evp.h>
#endif

namespace {

using Digest = std::array<unsigned char, 32>;

#ifdef _WIN32
void close_algorithm(void* handle) noexcept
{
    (void)BCryptCloseAlgorithmProvider(handle, 0);
}

void destroy_hash(void* handle) noexcept
{
    (void)BCryptDestroyHash(handle);
}
#endif

auto sha256(std::string_view input) -> std::string
{
    Digest digest{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE raw_algorithm{};
    if (BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error{"Could not open the SHA-256 provider"};
    }
    const std::unique_ptr<void, decltype(&close_algorithm)> algorithm{raw_algorithm, close_algorithm};
    BCRYPT_HASH_HANDLE raw_hash{};
    // CNG allocates the hash object; destroying the handle also releases its buffer.
    if (BCryptCreateHash(algorithm.get(), &raw_hash, nullptr, 0, nullptr, 0, 0) < 0) {
        throw std::runtime_error{"Could not create a SHA-256 hash"};
    }
    const std::unique_ptr<void, decltype(&destroy_hash)> hash{raw_hash, destroy_hash};
    while (!input.empty()) {
        const auto length = std::min(input.size(), static_cast<std::size_t>(std::numeric_limits<ULONG>::max()));
        // The API's input pointer lacks const qualification; it only reads these bytes.
        auto* bytes = reinterpret_cast<PUCHAR>(const_cast<char*>(input.data()));
        if (BCryptHashData(hash.get(), bytes, static_cast<ULONG>(length), 0) < 0) {
            throw std::runtime_error{"Could not update a SHA-256 hash"};
        }
        input.remove_prefix(length);
    }
    if (BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        throw std::runtime_error{"Could not finish a SHA-256 hash"};
    }
#else
    unsigned int length{};
    const auto* bytes = input.empty() ? "" : input.data();
    if (EVP_Digest(bytes, input.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1 ||
        length != digest.size()) {
        throw std::runtime_error{"Could not calculate a SHA-256 hash"};
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

} // namespace

namespace hfs {

auto make_page_identity(std::string_view final_url, std::string_view text)
    -> std::optional<PageIdentity>
{
    const auto canonical_url = resolve_http_url(final_url, "");
    if (!canonical_url) {
        return std::nullopt;
    }
    return PageIdentity{sha256(*canonical_url), sha256(text)};
}

} // namespace hfs
