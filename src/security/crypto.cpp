#include "ceebase/security/crypto.hpp"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace ceebase::security {

std::string Crypto::ToHex(const uint8_t* data, size_t len) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(hex_chars[(data[i] >> 4) & 0x0F]);
        result.push_back(hex_chars[data[i] & 0x0F]);
    }
    return result;
}

std::vector<uint8_t> Crypto::FromHex(std::string_view hex) {
    if (hex.size() % 2 != 0) return {};
    auto char_to_nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> bytes(hex.size() / 2);
    for (size_t i = 0; i < bytes.size(); ++i) {
        int hi = char_to_nibble(hex[i * 2]);
        int lo = char_to_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return {};
        bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return bytes;
}

bool Crypto::ConstantTimeCompare(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::vector<uint8_t> Crypto::SecureRandomBytes(size_t count) {
    std::vector<uint8_t> bytes(count);
#if defined(_WIN32)
    NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (BCRYPT_SUCCESS(status)) {
        return bytes;
    }
#endif
    std::random_device rd;
    auto seed = static_cast<uint64_t>(rd()) ^
                static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::mt19937_64 gen(seed);
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<uint8_t>(gen() & 0xFF);
    }
    return bytes;
}

std::string Crypto::SecureRandomHex(size_t byte_count) {
    auto bytes = SecureRandomBytes(byte_count);
    return ToHex(bytes.data(), bytes.size());
}

std::string Crypto::GenerateUUID() {
    auto b = SecureRandomBytes(16);
    // Set version 4: bits 12-15 of time_hi_and_version to 0100
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);
    // Set variant: bits 6-7 of clock_seq_hi_and_reserved to 10
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);

    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                  b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return std::string(buf, 36);
}

std::array<uint8_t, 32> Crypto::Sha256(const uint8_t* data, size_t len) noexcept {
    std::array<uint8_t, 32> hash{};
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        if (BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
            BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0);
            BCryptFinishHash(hHash, hash.data(), static_cast<ULONG>(hash.size()), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
#endif
    return hash;
}

std::array<uint8_t, 32> Crypto::Sha256(std::string_view input) noexcept {
    return Sha256(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

std::string Crypto::Sha256Hex(std::string_view input) {
    auto h = Sha256(input);
    return ToHex(h.data(), h.size());
}

std::array<uint8_t, 32> Crypto::HmacSha256(const uint8_t* key, size_t key_len,
                                           const uint8_t* data, size_t data_len) noexcept {
    std::array<uint8_t, 32> hash{};
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        if (BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, const_cast<PUCHAR>(key), static_cast<ULONG>(key_len), 0))) {
            BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(data_len), 0);
            BCryptFinishHash(hHash, hash.data(), static_cast<ULONG>(hash.size()), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
#endif
    return hash;
}

std::string Crypto::HmacSha256Hex(std::string_view key, std::string_view data) {
    auto h = HmacSha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                        reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return ToHex(h.data(), h.size());
}

std::string Crypto::HashPassword(std::string_view password, uint32_t iterations) {
    auto salt = SecureRandomBytes(16);
    std::array<uint8_t, 32> derived{};

#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        BCryptDeriveKeyPBKDF2(hAlg,
                              reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
                              static_cast<ULONG>(password.size()),
                              salt.data(),
                              static_cast<ULONG>(salt.size()),
                              iterations,
                              derived.data(),
                              static_cast<ULONG>(derived.size()),
                              0);
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
#endif

    std::string salt_hex = ToHex(salt.data(), salt.size());
    std::string hash_hex = ToHex(derived.data(), derived.size());
    return "$pbkdf2-sha256$i=" + std::to_string(iterations) + "$s=" + salt_hex + "$h=" + hash_hex;
}

bool Crypto::VerifyPassword(std::string_view password, std::string_view stored_hash) {
    // Format: $pbkdf2-sha256$i=<iter>$s=<salt_hex>$h=<hash_hex>
    if (!stored_hash.starts_with("$pbkdf2-sha256$")) return false;

    size_t i_pos = stored_hash.find("$i=");
    size_t s_pos = stored_hash.find("$s=");
    size_t h_pos = stored_hash.find("$h=");
    if (i_pos == std::string_view::npos || s_pos == std::string_view::npos || h_pos == std::string_view::npos) {
        return false;
    }

    std::string_view iter_str = stored_hash.substr(i_pos + 3, s_pos - (i_pos + 3));
    std::string_view salt_hex = stored_hash.substr(s_pos + 3, h_pos - (s_pos + 3));
    std::string_view hash_hex = stored_hash.substr(h_pos + 3);

    uint32_t iterations = 0;
    try {
        iterations = static_cast<uint32_t>(std::stoul(std::string(iter_str)));
    } catch (...) {
        return false;
    }

    auto salt = FromHex(salt_hex);
    if (salt.empty()) return false;

    std::array<uint8_t, 32> derived{};
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        BCryptDeriveKeyPBKDF2(hAlg,
                              reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
                              static_cast<ULONG>(password.size()),
                              salt.data(),
                              static_cast<ULONG>(salt.size()),
                              iterations,
                              derived.data(),
                              static_cast<ULONG>(derived.size()),
                              0);
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
#endif

    std::string calculated_hex = ToHex(derived.data(), derived.size());
    return ConstantTimeCompare(calculated_hex, hash_hex);
}

GeneratedApiKey Crypto::GenerateApiKey() {
    std::string secret = SecureRandomHex(24);
    std::string raw_key = "cb_live_" + secret;
    std::string prefix = raw_key.substr(0, 16); // e.g. cb_live_a1b2c3d4
    std::string hash = Sha256Hex(raw_key);
    return {raw_key, prefix, hash};
}

GeneratedToken Crypto::GenerateSessionToken() {
    std::string secret = SecureRandomHex(32);
    std::string raw_token = "cbs_" + secret;
    std::string hash = Sha256Hex(raw_token);
    return {raw_token, hash};
}

GeneratedOtp Crypto::GenerateOtp() {
    auto b = SecureRandomBytes(4);
    uint32_t num = (static_cast<uint32_t>(b[0]) << 24) |
                   (static_cast<uint32_t>(b[1]) << 16) |
                   (static_cast<uint32_t>(b[2]) << 8) |
                   static_cast<uint32_t>(b[3]);
    uint32_t code_num = num % 1000000;
    char buf[7];
    std::snprintf(buf, sizeof(buf), "%06u", code_num);
    std::string code(buf);
    std::string hash = Sha256Hex(code);
    return {code, hash};
}

std::string Crypto::GenerateBreakGlassToken() {
    return "cb_breakglass_" + SecureRandomHex(32);
}

} // namespace ceebase::security
