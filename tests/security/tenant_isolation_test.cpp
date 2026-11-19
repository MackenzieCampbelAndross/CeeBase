#include <gtest/gtest.h>
#include <filesystem>

#include "ceebase/identity/identity_manager.hpp"
#include "ceebase/identity/dedicated_deployment.hpp"
#include "ceebase/security/crypto.hpp"

namespace ceebase::identity {

class TenantIsolationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = (std::filesystem::temp_directory_path() / ("ceebase_tenant_test_" + security::Crypto::SecureRandomHex(8))).string();
        std::filesystem::create_directories(test_dir_);
        ident_mgr_ = std::make_unique<IdentityManager>(test_dir_);
        ASSERT_TRUE(ident_mgr_->Initialize().ok());

        // Setup Org 1: Acme Corp
        auto u1 = ident_mgr_->RegisterUser("acme_admin@acme.com", "AcmePassword123!").value();
        TenantContext ctx1{.user_id = u1.id, .is_authenticated = true};
        org1_ = ident_mgr_->CreateOrganization(ctx1, "Acme Corp", "acme-corp").value();
        acme_ctx_ = TenantContext{
            .user_id = u1.id,
            .org_id = org1_.id,
            .role = Role::ORG_OWNER,
            .is_authenticated = true
        };

        // Setup Org 2: Globex Inc
        auto u2 = ident_mgr_->RegisterUser("globex_admin@globex.com", "GlobexPassword123!").value();
        TenantContext ctx2{.user_id = u2.id, .is_authenticated = true};
        org2_ = ident_mgr_->CreateOrganization(ctx2, "Globex Inc", "globex-inc").value();
        globex_ctx_ = TenantContext{
            .user_id = u2.id,
            .org_id = org2_.id,
            .role = Role::ORG_OWNER,
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
    Organization org2_;
    TenantContext acme_ctx_;
    TenantContext globex_ctx_;
};

TEST_F(TenantIsolationTest, CrossTenantDatabaseAccessDenied) {
    // Acme registers its database
    ASSERT_TRUE(ident_mgr_->RegisterDatabase(acme_ctx_, "acme_vault").ok());

    // Acme can access its own database
    EXPECT_TRUE(ident_mgr_->AuthorizeDatabaseAccess(acme_ctx_, "acme_vault", Permission::DATA_READ).ok());
    EXPECT_TRUE(ident_mgr_->AuthorizeDatabaseAccess(acme_ctx_, "acme_vault", Permission::DATA_INSERT).ok());

    // Globex attempts to access Acme's database -> Strictly denied
    auto cross_res = ident_mgr_->AuthorizeDatabaseAccess(globex_ctx_, "acme_vault", Permission::DATA_READ);
    EXPECT_FALSE(cross_res.ok());
    EXPECT_EQ(cross_res.code(), StatusCode::Unauthorized);

    // Globex cannot register Acme's existing database name
    auto dup_reg = ident_mgr_->RegisterDatabase(globex_ctx_, "acme_vault");
    EXPECT_FALSE(dup_reg.ok());
}

TEST_F(TenantIsolationTest, SystemDatabaseDirectAccessForbidden) {
    // Neither Acme nor Globex can directly access _ceebase_system
    auto s1 = ident_mgr_->AuthorizeDatabaseAccess(acme_ctx_, "_ceebase_system", Permission::DATA_READ);
    EXPECT_FALSE(s1.ok());
    EXPECT_EQ(s1.code(), StatusCode::PermissionDenied);

    auto s2 = ident_mgr_->RegisterDatabase(acme_ctx_, "_ceebase_system");
    EXPECT_FALSE(s2.ok());

    auto s3 = ident_mgr_->AuthorizeDatabaseAccess(acme_ctx_, "_ceebase_system.cbdb", Permission::DATA_READ);
    EXPECT_FALSE(s3.ok());
}

TEST_F(TenantIsolationTest, RoleBasedAccessControlEnforcement) {
    ASSERT_TRUE(ident_mgr_->RegisterDatabase(acme_ctx_, "acme_prod").ok());

    TenantContext analyst_ctx{
        .user_id = "user_analyst",
        .org_id = org1_.id,
        .role = Role::READ_ONLY_ANALYST,
        .is_authenticated = true
    };

    // Analyst can read
    EXPECT_TRUE(ident_mgr_->AuthorizeDatabaseAccess(analyst_ctx, "acme_prod", Permission::DATA_READ).ok());

    // Analyst cannot write or delete
    auto insert_perm = ident_mgr_->AuthorizeDatabaseAccess(analyst_ctx, "acme_prod", Permission::DATA_INSERT);
    EXPECT_FALSE(insert_perm.ok());
    EXPECT_EQ(insert_perm.code(), StatusCode::PermissionDenied);

    auto delete_perm = ident_mgr_->AuthorizeDatabaseAccess(analyst_ctx, "acme_prod", Permission::DATA_DELETE);
    EXPECT_FALSE(delete_perm.ok());
    EXPECT_EQ(delete_perm.code(), StatusCode::PermissionDenied);

    // Developer can write
    TenantContext dev_ctx{
        .user_id = "user_dev",
        .org_id = org1_.id,
        .role = Role::DEVELOPER,
        .is_authenticated = true
    };
    EXPECT_TRUE(ident_mgr_->AuthorizeDatabaseAccess(dev_ctx, "acme_prod", Permission::DATA_INSERT).ok());
}

TEST_F(TenantIsolationTest, OrganizationDatabaseQuotaEnforcement) {
    // Org default max is 10 databases
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(ident_mgr_->RegisterDatabase(acme_ctx_, "db_" + std::to_string(i)).ok());
    }

    // 11th registration exceeds quota
    auto quota_exceeded = ident_mgr_->RegisterDatabase(acme_ctx_, "db_11_fail");
    EXPECT_FALSE(quota_exceeded.ok());
    EXPECT_EQ(quota_exceeded.code(), StatusCode::ResourceExhausted);
}

TEST_F(TenantIsolationTest, DedicatedDeploymentProvisioning) {
    DedicatedDeploymentManager ded_mgr;
    std::string base_dir = (std::filesystem::path(test_dir_) / "dedicated_clusters").string();

    auto prov_res = ded_mgr.ProvisionDeployment("org_enterprise_99", "Enterprise Production", base_dir, 9090);
    ASSERT_TRUE(prov_res.ok());
    EXPECT_EQ(prov_res.value().port, 9090);
    EXPECT_EQ(prov_res.value().status, "ACTIVE");

    // Verify directories created
    std::filesystem::path conf_file = std::filesystem::path(base_dir) / "org_enterprise_99" / "conf" / "ceebase_dedicated.conf";
    EXPECT_TRUE(std::filesystem::exists(conf_file));

    // Duplicate port rejected
    auto dup_port = ded_mgr.ProvisionDeployment("org_another", "Another Cluster", base_dir, 9090);
    EXPECT_FALSE(dup_port.ok());
}

} // namespace ceebase::identity
