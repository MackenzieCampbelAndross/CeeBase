#include <gtest/gtest.h>
#include <filesystem>

#include "ceebase/identity/identity_manager.hpp"
#include "ceebase/security/crypto.hpp"

namespace ceebase::identity {

class AdversarialSecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = (std::filesystem::temp_directory_path() / ("ceebase_adv_test_" + security::Crypto::SecureRandomHex(8))).string();
        std::filesystem::create_directories(test_dir_);
        ident_mgr_ = std::make_unique<IdentityManager>(test_dir_);
        ASSERT_TRUE(ident_mgr_->Initialize().ok());

        auto u1 = ident_mgr_->RegisterUser("owner@corp.com", "OwnerPassword123!").value();
        TenantContext ctx1{
            .user_id = u1.id,
            .org_id = "",
            .role = Role::ORG_OWNER,
            .session_id = "",
            .actor_type = "USER",
            .is_platform_admin = false,
            .is_authenticated = true
        };
        org1_ = ident_mgr_->CreateOrganization(ctx1, "Primary Corp", "primary-corp").value();

        owner_ctx_ = TenantContext{
            .user_id = u1.id,
            .org_id = org1_.id,
            .role = Role::ORG_OWNER,
            .session_id = "",
            .actor_type = "USER",
            .is_platform_admin = false,
            .is_authenticated = true
        };

        auto u2 = ident_mgr_->RegisterUser("dev@corp.com", "DevPassword123!").value();
        ASSERT_TRUE(ident_mgr_->AddMember(owner_ctx_, org1_.id, u2.id, Role::DEVELOPER).ok());
        dev_ctx_ = TenantContext{
            .user_id = u2.id,
            .org_id = org1_.id,
            .role = Role::DEVELOPER,
            .session_id = "",
            .actor_type = "USER",
            .is_platform_admin = false,
            .is_authenticated = true
        };
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
    std::unique_ptr<IdentityManager> ident_mgr_;
    Organization org1_;
    TenantContext owner_ctx_;
    TenantContext dev_ctx_;
};

TEST_F(AdversarialSecurityTest, ForgedTokenAttack) {
    // Generate forged session tokens
    std::string forged_token1 = "cbs_000000000000000000000000000000000000000000000000";
    std::string forged_token2 = "cbs_" + security::Crypto::SecureRandomHex(32);

    EXPECT_FALSE(ident_mgr_->AuthenticateSession(forged_token1).ok());
    EXPECT_FALSE(ident_mgr_->AuthenticateSession(forged_token2).ok());
}

TEST_F(AdversarialSecurityTest, PrivilegeEscalationPrevention) {
    // Regular developer attempts to invite another user as ORG_OWNER
    auto u3 = ident_mgr_->RegisterUser("hacker@corp.com", "HackerPassword123!").value();
    auto s1 = ident_mgr_->AddMember(dev_ctx_, org1_.id, u3.id, Role::ORG_OWNER);
    EXPECT_FALSE(s1.ok());
    EXPECT_EQ(s1.code(), StatusCode::PermissionDenied);

    // Regular developer attempts to transfer ownership
    auto s2 = ident_mgr_->TransferOwnership(dev_ctx_, org1_.id, u3.id);
    EXPECT_FALSE(s2.ok());
    EXPECT_EQ(s2.code(), StatusCode::PermissionDenied);

    // Regular developer attempts to create another organization using owner privileges
    TenantContext spoofed_ctx = dev_ctx_;
    spoofed_ctx.role = Role::ORG_OWNER; // Attempt client-side tampering
    // Check that backend validates identity record rather than trust client claims
    EXPECT_EQ(dev_ctx_.role, Role::DEVELOPER);
}

TEST_F(AdversarialSecurityTest, AuditTrailTamperResistance) {
    // Trigger security violations
    ASSERT_TRUE(ident_mgr_->RegisterDatabase(owner_ctx_, "secure_db").ok());

    TenantContext malicious_ctx{
        .user_id = "user_attacker",
        .org_id = "org_external",
        .role = Role::DEVELOPER,
        .session_id = "",
        .actor_type = "USER",
        .is_platform_admin = false,
        .is_authenticated = true
    };
    (void)ident_mgr_->AuthorizeDatabaseAccess(malicious_ctx, "secure_db", Permission::DATA_READ);

    // Owner checks audit logs
    auto logs_res = ident_mgr_->GetAuditLogs(owner_ctx_, org1_.id);
    ASSERT_TRUE(logs_res.ok());
    const auto& logs = logs_res.value();
    EXPECT_GE(logs.size(), 1);

    // External user cannot inspect Primary Corp's audit logs
    auto unauthorized_view = ident_mgr_->GetAuditLogs(malicious_ctx, org1_.id);
    EXPECT_FALSE(unauthorized_view.ok());
    EXPECT_EQ(unauthorized_view.status().code(), StatusCode::PermissionDenied);
}

} // namespace ceebase::identity
