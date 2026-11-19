#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ceebase::security {

struct GeneratedApiKey {
    std::string raw_key;   // Displayed once: cb_live_<hex>
    std::string prefix;    // Safe prefix for lookup: cb_live_<8 hex>
    std::string hash;      // SHA-256 hash stored in DB
};

struct GeneratedToken {
    std::string raw_token; // Bearer token: cbs_<hex>
    std::string hash;      // SHA-256 hash stored in DB
};

struct GeneratedOtp {
    std::string code;      // 6-digit OTP: e.g. "482910"
    std::string hash;      // SHA-256 hash stored in DB
};

class Crypto {
public:
    // CSPRNG
    static std::vector<uint8_t> SecureRandomBytes(size_t count);
    static std::string SecureRandomHex(size_t byte_count);
    static std::string GenerateUUID();

    // Constant-time comparison for timing attack prevention
    static bool ConstantTimeCompare(std::string_view a, std::string_view b) noexcept;

    // FIPS 180-4 SHA-256
    static std::array<uint8_t, 32> Sha256(const uint8_t* data, size_t len) noexcept;
    static std::array<uint8_t, 32> Sha256(std::string_view input) noexcept;
    static std::string Sha256Hex(std::string_view input);

    // RFC 2104 HMAC-SHA256
    static std::array<uint8_t, 32> HmacSha256(const uint8_t* key, size_t key_len,
                                              const uint8_t* data, size_t data_len) noexcept;
    static std::string HmacSha256Hex(std::string_view key, std::string_view data);

    // Password Hashing (PBKDF2-HMAC-SHA256)
    static std::string HashPassword(std::string_view password, uint32_t iterations = 10000);
    static bool VerifyPassword(std::string_view password, std::string_view stored_hash);

    // Token and Secret Generators
    static GeneratedApiKey GenerateApiKey();
    static GeneratedToken GenerateSessionToken();
    static GeneratedOtp GenerateOtp();
    static std::string GenerateBreakGlassToken();

    // Hex helpers
    static std::string ToHex(const uint8_t* data, size_t len);
    static std::vector<uint8_t> FromHex(std::string_view hex);
};

} // namespace ceebase::security
