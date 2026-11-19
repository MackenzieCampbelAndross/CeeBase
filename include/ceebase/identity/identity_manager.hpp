#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ceebase/common/result.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/identity/identity_types.hpp"
#include "ceebase/security/crypto.hpp"

namespace ceebase {
class DiskManager;
class BufferPoolManager;
class LogManager;
class Catalog;
class TransactionManager;
class RecoveryEngine;
class ExecutionEngine;
} // namespace ceebase

namespace ceebase::identity {

class IdentityManager {
public:
    explicit IdentityManager(std::string system_db_dir, std::string break_glass_secret = "");
    ~IdentityManager();

    IdentityManager(const IdentityManager&) = delete;
    IdentityManager& operator=(const IdentityManager&) = delete;

    // Bootstrap & Lifecycle
    Status Initialize();
    [[nodiscard]] bool IsOperational() const noexcept;
    void Shutdown();

    // System Protection
    [[nodiscard]] static bool IsSystemDatabase(std::string_view db_name) noexcept;

    // Break-Glass Administrative Recovery
    Status BreakGlassResetAdmin(std::string_view break_glass_token, std::string_view new_admin_password);

    // Authentication: Password
    Result<User> RegisterUser(std::string_view email, std::string_view password);
    Result<User> AuthenticatePassword(std::string_view email, std::string_view password, std::string_view ip = "127.0.0.1");

    // Authentication: OTP
    Result<security::GeneratedOtp> CreateOtpChallenge(std::string_view email);
    Result<User> VerifyOtp(std::string_view email, std::string_view otp_code, std::string_view ip = "127.0.0.1");

    // Authentication: Session
    Result<security::GeneratedToken> CreateSession(std::string_view user_id, std::string_view org_id, uint64_t ttl_seconds = 86400);
    Result<TenantContext> AuthenticateSession(std::string_view token, std::string_view ip = "127.0.0.1");
    Status RevokeSession(std::string_view session_id);
    Status RevokeAllUserSessions(std::string_view user_id);

    // Authentication: API Keys
    Result<security::GeneratedApiKey> CreateApiKey(std::string_view org_id, std::string_view actor_id,
                                                   std::string_view name, Role role, uint64_t ttl_seconds = 0);
    Result<TenantContext> AuthenticateApiKey(std::string_view api_key, std::string_view ip = "127.0.0.1");
    Status RevokeApiKey(std::string_view key_id);

    // Service Account & Agent Identities
    Result<ServiceAccount> CreateServiceAccount(const TenantContext& caller, std::string_view org_id,
                                                std::string_view name, Role role);
    Result<AgentIdentity> CreateAgentIdentity(const TenantContext& caller, std::string_view org_id,
                                              std::string_view name, std::string_view capabilities,
                                              std::string_view model_id, Role role);

    // Organizations & Memberships
    Result<Organization> CreateOrganization(const TenantContext& caller, std::string_view name, std::string_view slug);
    Result<Organization> GetOrganization(std::string_view org_id);
    Status AddMember(const TenantContext& caller, std::string_view org_id, std::string_view user_id, Role role);
    Status RemoveMember(const TenantContext& caller, std::string_view org_id, std::string_view user_id);
    Status TransferOwnership(const TenantContext& caller, std::string_view org_id, std::string_view new_owner_id);
    std::vector<Membership> GetUserMemberships(std::string_view user_id);

    // Database Tenant Isolation & Routing
    Status RegisterDatabase(const TenantContext& caller, std::string_view db_name);
    Status AuthorizeDatabaseAccess(const TenantContext& caller, std::string_view db_name, Permission perm);
    Result<std::vector<std::string>> ListDatabases(const TenantContext& caller);

    // Audit Logging
    void LogAudit(AuditEvent event);
    Result<std::vector<AuditEvent>> GetAuditLogs(const TenantContext& caller, std::string_view org_id);

    // Break-glass secret accessor
    [[nodiscard]] const std::string& GetBreakGlassSecret() const noexcept { return break_glass_secret_; }
    [[nodiscard]] std::string GetSystemDatabasePath() const;

private:
    Status EnsureSystemSchema();
    Status SeedInitialAdmin();
    Status LoadAllEntitiesFromDisk();

    std::string system_db_dir_;
    std::string break_glass_secret_;
    bool operational_{false};

    mutable std::mutex mutex_;

    // Core CeeBase storage engine components for system DB
    std::unique_ptr<DiskManager> disk_mgr_;
    std::unique_ptr<BufferPoolManager> bpm_;
    std::unique_ptr<LogManager> log_mgr_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<TransactionManager> txn_mgr_;
    std::unique_ptr<RecoveryEngine> recovery_;
    std::unique_ptr<ExecutionEngine> engine_;

    // In-memory indexed caches for constant-time lookup, backed by system DB
    std::unordered_map<std::string, User> users_by_id_;
    std::unordered_map<std::string, std::string> user_id_by_email_;
    std::unordered_map<std::string, Organization> orgs_by_id_;
    std::unordered_map<std::string, std::string> org_id_by_slug_;
    std::vector<Membership> memberships_;
    std::unordered_map<std::string, Session> sessions_by_hash_;
    std::unordered_map<std::string, ApiKey> api_keys_by_hash_;
    std::vector<ServiceAccount> service_accounts_;
    std::vector<AgentIdentity> agent_identities_;
    std::vector<AuditEvent> audit_events_;
    std::unordered_map<std::string, OtpChallenge> otp_by_email_;
    std::unordered_map<std::string, DatabaseAssignment> db_assignments_;
};

} // namespace ceebase::identity
