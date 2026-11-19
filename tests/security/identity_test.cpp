#include <gtest/gtest.h>
#include <filesystem>
#include <thread>

#include "ceebase/identity/identity_manager.hpp"
#include "ceebase/security/crypto.hpp"

namespace ceebase::identity {

class IdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = (std::filesystem::temp_directory_path() / ("ceebase_ident_test_" + security::Crypto::SecureRandomHex(8))).string();
        std::filesystem::create_directories(test_dir_);
        break_glass_secret_ = "breakglass_secret_token_123456789";
        ident_mgr_ = std::make_unique<IdentityManager>(test_dir_, break_glass_secret_);
        ASSERT_TRUE(ident_mgr_->Initialize().ok());
    }

    void TearDown() override {
        if (ident_mgr_) {
            ident_mgr_->Shutdown();
            ident_mgr_.reset();
        }
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::string test_dir_;
    std::string break_glass_secret_;
    std::unique_ptr<IdentityManager> ident_mgr_;
};

TEST_F(IdentityTest, CryptoPrimitivesValidation) {
    // SHA256 deterministic test
    auto hash1 = security::Crypto::Sha256Hex("hello");
    auto hash2 = security::Crypto::Sha256Hex("hello");
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash1, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");

    // Constant-time comparison
    EXPECT_TRUE(security::Crypto::ConstantTimeCompare("secret_token_abc", "secret_token_abc"));
    EXPECT_FALSE(security::Crypto::ConstantTimeCompare("secret_token_abc", "secret_token_abd"));
    EXPECT_FALSE(security::Crypto::ConstantTimeCompare("short", "longer_secret"));

    // UUID v4 format
    std::string uuid = security::Crypto::GenerateUUID();
    EXPECT_EQ(uuid.size(), 36);
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');
    EXPECT_EQ(uuid[14], '4'); // Version 4

    // Password Hashing and Verification
    std::string pwd = "CorrectHorseBatteryStaple!";
    std::string hashed = security::Crypto::HashPassword(pwd, 1000);
    EXPECT_TRUE(security::Crypto::VerifyPassword(pwd, hashed));
    EXPECT_FALSE(security::Crypto::VerifyPassword("WrongPassword!", hashed));
}

TEST_F(IdentityTest, PasswordRegistrationAndAuthentication) {
    auto reg_res = ident_mgr_->RegisterUser("alice@example.com", "MySecureP@ssw0rd");
    ASSERT_TRUE(reg_res.ok());
    EXPECT_EQ(reg_res.value().email, "alice@example.com");
    EXPECT_EQ(reg_res.value().status, "ACTIVE");

    // Duplicate email rejected
    auto dup_res = ident_mgr_->RegisterUser("alice@example.com", "AnotherP@ssw0rd");
    EXPECT_FALSE(dup_res.ok());
    EXPECT_TRUE(dup_res.status().IsAlreadyExists());

    // Valid authentication
    auto auth_res = ident_mgr_->AuthenticatePassword("alice@example.com", "MySecureP@ssw0rd");
    ASSERT_TRUE(auth_res.ok());
    EXPECT_EQ(auth_res.value().email, "alice@example.com");

    // Bad password
    auto bad_auth = ident_mgr_->AuthenticatePassword("alice@example.com", "WrongPassword");
    EXPECT_FALSE(bad_auth.ok());
}

TEST_F(IdentityTest, BruteForceLockoutProtection) {
    ASSERT_TRUE(ident_mgr_->RegisterUser("bob@example.com", "B0bSecurePass!").ok());

    // Fail 4 times (threshold is 5)
    for (int i = 0; i < 4; ++i) {
        auto res = ident_mgr_->AuthenticatePassword("bob@example.com", "WrongPass");
        EXPECT_FALSE(res.ok());
    }

    // 5th failure triggers lockout
    auto lock_res = ident_mgr_->AuthenticatePassword("bob@example.com", "WrongPass");
    EXPECT_FALSE(lock_res.ok());
    EXPECT_EQ(lock_res.status().code(), StatusCode::ResourceExhausted);

    // Even with correct password, account remains locked
    auto locked_login = ident_mgr_->AuthenticatePassword("bob@example.com", "B0bSecurePass!");
    EXPECT_FALSE(locked_login.ok());
    EXPECT_EQ(locked_login.status().code(), StatusCode::ResourceExhausted);
}

TEST_F(IdentityTest, PasswordlessOtpLifecycle) {
    ASSERT_TRUE(ident_mgr_->RegisterUser("charlie@example.com", "Ch@rliePass123").ok());

    auto otp_res = ident_mgr_->CreateOtpChallenge("charlie@example.com");
    ASSERT_TRUE(otp_res.ok());
    std::string code = otp_res.value().code;
    EXPECT_EQ(code.size(), 6);

    // Bad OTP code rejected
    auto bad_otp = ident_mgr_->VerifyOtp("charlie@example.com", "000000");
    EXPECT_FALSE(bad_otp.ok());

    // Correct OTP code succeeds
    auto good_otp = ident_mgr_->VerifyOtp("charlie@example.com", code);
    ASSERT_TRUE(good_otp.ok());

    // Single-use: reusing same OTP is rejected
    auto reuse_otp = ident_mgr_->VerifyOtp("charlie@example.com", code);
    EXPECT_FALSE(reuse_otp.ok());
}

TEST_F(IdentityTest, SessionTokenManagementAndRevocation) {
    auto u_res = ident_mgr_->RegisterUser("david@example.com", "D@vidPass456");
    ASSERT_TRUE(u_res.ok());

    TenantContext caller{
        .user_id = u_res.value().id,
        .org_id = "",
        .role = Role::ORG_OWNER,
        .is_authenticated = true
    };
    auto org_res = ident_mgr_->CreateOrganization(caller, "David Tech", "david-tech");
    ASSERT_TRUE(org_res.ok());

    auto sess_res = ident_mgr_->CreateSession(u_res.value().id, org_res.value().id, 3600);
    ASSERT_TRUE(sess_res.ok());
    std::string token = sess_res.value().raw_token;
    EXPECT_TRUE(token.starts_with("cbs_"));

    // Authenticate with session token
    auto auth_ctx = ident_mgr_->AuthenticateSession(token);
    ASSERT_TRUE(auth_ctx.ok());
    EXPECT_EQ(auth_ctx.value().user_id, u_res.value().id);
    EXPECT_EQ(auth_ctx.value().org_id, org_res.value().id);
    EXPECT_EQ(auth_ctx.value().role, Role::ORG_OWNER);

    // Revoke session
    ASSERT_TRUE(ident_mgr_->RevokeSession(auth_ctx.value().session_id).ok());

    // Revoked session rejected
    auto revoked_ctx = ident_mgr_->AuthenticateSession(token);
    EXPECT_FALSE(revoked_ctx.ok());
}

TEST_F(IdentityTest, ApiKeyGenerationAndAuthentication) {
    auto u_res = ident_mgr_->RegisterUser("eva@example.com", "Ev@SecurePass789");
    ASSERT_TRUE(u_res.ok());
    TenantContext caller{.user_id = u_res.value().id, .is_authenticated = true};
    auto org_res = ident_mgr_->CreateOrganization(caller, "Eva AI", "eva-ai");
    ASSERT_TRUE(org_res.ok());

    auto key_res = ident_mgr_->CreateApiKey(org_res.value().id, u_res.value().id, "Production Key", Role::DEVELOPER, 0);
    ASSERT_TRUE(key_res.ok());
    std::string raw_key = key_res.value().raw_key;
    EXPECT_TRUE(raw_key.starts_with("cb_live_"));

    // Authenticate with API key
    auto key_ctx = ident_mgr_->AuthenticateApiKey(raw_key);
    ASSERT_TRUE(key_ctx.ok());
    EXPECT_EQ(key_ctx.value().org_id, org_res.value().id);
    EXPECT_EQ(key_ctx.value().role, Role::DEVELOPER);

    // Revoke key
    std::string key_id = key_ctx.value().session_id;
    ASSERT_TRUE(ident_mgr_->RevokeApiKey(key_id).ok());

    // Revoked key rejected
    auto rev_ctx = ident_mgr_->AuthenticateApiKey(raw_key);
    EXPECT_FALSE(rev_ctx.ok());
}

TEST_F(IdentityTest, BreakGlassAdministrativeRecovery) {
    // Wrong break-glass token fails
    auto bad_reset = ident_mgr_->BreakGlassResetAdmin("wrong_token", "NewP@ssw0rd2026!");
    EXPECT_FALSE(bad_reset.ok());

    // Valid break-glass token resets admin
    auto good_reset = ident_mgr_->BreakGlassResetAdmin(break_glass_secret_, "NewAdminPass2026!");
    ASSERT_TRUE(good_reset.ok());

    // Platform admin logs in with new password
    auto admin_login = ident_mgr_->AuthenticatePassword("admin@ceebase.local", "NewAdminPass2026!");
    ASSERT_TRUE(admin_login.ok());
}

} // namespace ceebase::identity
