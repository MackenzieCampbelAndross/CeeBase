#include "ceebase/identity/identity_manager.hpp"

#include <chrono>
#include <filesystem>
#include <algorithm>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/catalog/catalog.hpp"
#include "ceebase/concurrency/transaction_manager.hpp"
#include "ceebase/execution/execution_engine.hpp"
#include "ceebase/recovery/log_manager.hpp"
#include "ceebase/recovery/recovery_engine.hpp"
#include "ceebase/storage/disk_manager.hpp"

namespace ceebase::identity {

using namespace ceebase::security;

namespace {

uint64_t CurrentTimestamp() noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

IdentityManager::IdentityManager(std::string system_db_dir, std::string break_glass_secret)
    : system_db_dir_(std::move(system_db_dir)),
      break_glass_secret_(std::move(break_glass_secret)) {
    if (break_glass_secret_.empty()) {
        break_glass_secret_ = Crypto::GenerateBreakGlassToken();
    }
}

IdentityManager::~IdentityManager() {
    Shutdown();
}

std::string IdentityManager::GetSystemDatabasePath() const {
    std::filesystem::path p(system_db_dir_);
    return (p / "_ceebase_system.cbdb").string();
}

bool IdentityManager::IsSystemDatabase(std::string_view db_name) noexcept {
    if (db_name == "_ceebase_system" || db_name == "_ceebase_system.cbdb") return true;
    if (db_name.starts_with("_ceebase_")) return true;
    if (db_name.find("_ceebase_system") != std::string_view::npos) return true;
    return false;
}

bool IdentityManager::IsOperational() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return operational_;
}

void IdentityManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return;

    if (bpm_) {
        (void)bpm_->FlushAllPages();
    }
    if (disk_mgr_) {
        (void)disk_mgr_->Sync();
        disk_mgr_->Close();
    }

    engine_.reset();
    recovery_.reset();
    txn_mgr_.reset();
    log_mgr_.reset();
    catalog_.reset();
    bpm_.reset();
    disk_mgr_.reset();

    operational_ = false;
}

Status IdentityManager::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    operational_ = false;

    std::error_code ec;
    std::filesystem::create_directories(system_db_dir_, ec);

    std::string db_path = GetSystemDatabasePath();
    std::string wal_path = (std::filesystem::path(system_db_dir_) / "_ceebase_system.wal").string();

    disk_mgr_ = std::make_unique<DiskManager>();
    Status s;
    if (std::filesystem::exists(db_path)) {
        s = disk_mgr_->OpenDatabase(db_path);
    } else {
        s = disk_mgr_->CreateDatabase(db_path);
    }
    if (!s.ok()) {
        return Status::Internal("Failed to initialize identity system storage: " + s.ToString());
    }

    bpm_ = std::make_unique<BufferPoolManager>(32, disk_mgr_.get());
    catalog_ = std::make_unique<Catalog>(bpm_.get());
    log_mgr_ = std::make_unique<LogManager>(wal_path);
    txn_mgr_ = std::make_unique<TransactionManager>(log_mgr_.get(), bpm_.get(), catalog_.get());
    recovery_ = std::make_unique<RecoveryEngine>(log_mgr_.get(), bpm_.get(), catalog_.get());

    s = recovery_->Recover();
    if (!s.ok()) {
        Shutdown();
        return Status::Internal("Identity database recovery failure (failing closed): " + s.ToString());
    }

    engine_ = std::make_unique<ExecutionEngine>(bpm_.get(), catalog_.get(), txn_mgr_.get(), recovery_.get());

    s = EnsureSystemSchema();
    if (!s.ok()) {
        Shutdown();
        return s;
    }

    s = SeedInitialAdmin();
    if (!s.ok()) {
        Shutdown();
        return s;
    }

    operational_ = true;
    return Status::Ok();
}

Status IdentityManager::EnsureSystemSchema() {
    // Persistent system tables in _ceebase_system
    std::vector<std::string> ddl = {
        "CREATE TABLE sys_users (id TEXT, email TEXT, password_hash TEXT, status TEXT, failed_attempts INTEGER, locked_until_ts BIGINT, created_at_ts BIGINT, updated_at_ts BIGINT);",
        "CREATE TABLE sys_organizations (id TEXT, name TEXT, slug TEXT, status TEXT, max_databases INTEGER, max_storage_bytes BIGINT, max_qpm INTEGER, created_at_ts BIGINT);",
        "CREATE TABLE sys_memberships (id TEXT, org_id TEXT, user_id TEXT, role TEXT, status TEXT, joined_at_ts BIGINT);",
        "CREATE TABLE sys_sessions (id TEXT, token_hash TEXT, user_id TEXT, org_id TEXT, role TEXT, expires_at_ts BIGINT, created_at_ts BIGINT, last_active_ts BIGINT, revoked INTEGER);",
        "CREATE TABLE sys_api_keys (id TEXT, name TEXT, key_prefix TEXT, key_hash TEXT, org_id TEXT, actor_id TEXT, role TEXT, expires_at_ts BIGINT, revoked INTEGER, created_at_ts BIGINT);",
        "CREATE TABLE sys_service_accounts (id TEXT, org_id TEXT, name TEXT, role TEXT, created_at_ts BIGINT);",
        "CREATE TABLE sys_agent_identities (id TEXT, org_id TEXT, name TEXT, capabilities TEXT, model_id TEXT, role TEXT, created_at_ts BIGINT);",
        "CREATE TABLE sys_audit_events (id TEXT, timestamp_ts BIGINT, actor_id TEXT, actor_type TEXT, org_id TEXT, event_type TEXT, target_resource TEXT, outcome TEXT, ip_address TEXT, details TEXT);",
        "CREATE TABLE sys_otp_challenges (id TEXT, user_id TEXT, email TEXT, otp_hash TEXT, attempts INTEGER, expires_at_ts BIGINT, used INTEGER);",
        "CREATE TABLE sys_db_assignments (db_name TEXT, org_id TEXT, created_at_ts BIGINT);"
    };

    for (const auto& sql : ddl) {
        if (!catalog_->GetTable(sql.substr(13, sql.find(' ', 13) - 13))) {
            auto qres = engine_->Execute(sql);
            if (!qres.success && qres.error_message.find("ALREADY_EXISTS") == std::string::npos) {
                return Status::Internal("Failed creating system table: " + qres.error_message);
            }
        }
    }

    return Status::Ok();
}

Status IdentityManager::SeedInitialAdmin() {
    // If no users exist, create default Platform Admin
    if (users_by_id_.empty()) {
        std::string admin_id = "user_root_admin";
        std::string email = "admin@ceebase.local";
        std::string pwd_hash = Crypto::HashPassword("CeeBaseAdmin2026!Secure");
        uint64_t now = CurrentTimestamp();

        User admin_user{
            .id = admin_id,
            .email = email,
            .password_hash = pwd_hash,
            .status = "ACTIVE",
            .failed_attempts = 0,
            .locked_until_ts = 0,
            .created_at_ts = now,
            .updated_at_ts = now
        };
        users_by_id_[admin_id] = admin_user;
        user_id_by_email_[email] = admin_id;

        // Create default system organization
        std::string org_id = "org_system_platform";
        Organization sys_org{
            .id = org_id,
            .name = "Platform Root",
            .slug = "system",
            .status = "ACTIVE",
            .max_databases = 1000,
            .max_storage_bytes = 1099511627776ULL, // 1 TB
            .max_queries_per_min = 100000,
            .created_at_ts = now
        };
        orgs_by_id_[org_id] = sys_org;
        org_id_by_slug_["system"] = org_id;

        // Add platform admin membership
        Membership m{
            .id = Crypto::GenerateUUID(),
            .org_id = org_id,
            .user_id = admin_id,
            .role = Role::PLATFORM_ADMIN,
            .status = "ACTIVE",
            .joined_at_ts = now
        };
        memberships_.push_back(m);
    }
    return Status::Ok();
}

Status IdentityManager::BreakGlassResetAdmin(std::string_view break_glass_token, std::string_view new_admin_password) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Status::Internal("Identity manager not operational");

    if (!Crypto::ConstantTimeCompare(break_glass_token, break_glass_secret_)) {
        AuditEvent ev{
            .id = Crypto::GenerateUUID(),
            .timestamp_ts = CurrentTimestamp(),
            .actor_id = "BREAK_GLASS_ACTOR",
            .actor_type = "BREAK_GLASS",
            .org_id = "org_system_platform",
            .event_type = "SECURITY_ADMIN",
            .target_resource = "admin@ceebase.local",
            .outcome = "DENIED",
            .ip_address = "127.0.0.1",
            .details = "Invalid break-glass recovery token presented"
        };
        audit_events_.push_back(ev);
        return Status::Unauthorized("Invalid break glass recovery token");
    }

    auto it = user_id_by_email_.find("admin@ceebase.local");
    if (it == user_id_by_email_.end()) {
        return Status::NotFound("Platform administrator account missing");
    }

    User& admin = users_by_id_[it->second];
    admin.password_hash = Crypto::HashPassword(new_admin_password);
    admin.failed_attempts = 0;
    admin.locked_until_ts = 0;
    admin.status = "ACTIVE";
    admin.updated_at_ts = CurrentTimestamp();

    AuditEvent ev{
        .id = Crypto::GenerateUUID(),
        .timestamp_ts = CurrentTimestamp(),
        .actor_id = "BREAK_GLASS_ACTOR",
        .actor_type = "BREAK_GLASS",
        .org_id = "org_system_platform",
        .event_type = "SECURITY_ADMIN",
        .target_resource = "admin@ceebase.local",
        .outcome = "SUCCESS",
        .ip_address = "127.0.0.1",
        .details = "Platform admin credentials reset via break-glass protocol"
    };
    audit_events_.push_back(ev);

    return Status::Ok();
}

Result<User> IdentityManager::RegisterUser(std::string_view email, std::string_view password) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<User>::Failure(Status::Internal("Identity system unavailable"));

    if (email.find('@') == std::string_view::npos || email.find('.') == std::string_view::npos) {
        return Result<User>::Failure(Status::InvalidArgument("Invalid email format"));
    }
    if (password.size() < 8) {
        return Result<User>::Failure(Status::InvalidArgument("Password must be at least 8 characters"));
    }

    std::string email_str(email);
    if (user_id_by_email_.contains(email_str)) {
        return Result<User>::Failure(Status::AlreadyExists("User with email already exists"));
    }

    std::string user_id = "user_" + Crypto::GenerateUUID();
    std::string pwd_hash = Crypto::HashPassword(password);
    uint64_t now = CurrentTimestamp();

    User u{
        .id = user_id,
        .email = email_str,
        .password_hash = pwd_hash,
        .status = "ACTIVE",
        .failed_attempts = 0,
        .locked_until_ts = 0,
        .created_at_ts = now,
        .updated_at_ts = now
    };

    users_by_id_[user_id] = u;
    user_id_by_email_[email_str] = user_id;

    AuditEvent ev{
        .id = Crypto::GenerateUUID(),
        .timestamp_ts = now,
        .actor_id = user_id,
        .actor_type = "USER",
        .org_id = "",
        .event_type = "USER_REGISTER",
        .target_resource = email_str,
        .outcome = "SUCCESS",
        .ip_address = "127.0.0.1",
        .details = "User registered successfully"
    };
    audit_events_.push_back(ev);

    return Result<User>::Success(u);
}

Result<User> IdentityManager::AuthenticatePassword(std::string_view email, std::string_view password, std::string_view ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<User>::Failure(Status::Internal("Identity system unavailable"));

    std::string email_str(email);
    auto it = user_id_by_email_.find(email_str);
    if (it == user_id_by_email_.end()) {
        return Result<User>::Failure(Status::Unauthorized("Invalid credentials"));
    }

    User& user = users_by_id_[it->second];
    uint64_t now = CurrentTimestamp();

    // Check account lockout
    if (user.locked_until_ts > now) {
        AuditEvent ev{
            .id = Crypto::GenerateUUID(),
            .timestamp_ts = now,
            .actor_id = user.id,
            .actor_type = "USER",
            .org_id = "",
            .event_type = "AUTH_LOCKED",
            .target_resource = email_str,
            .outcome = "DENIED",
            .ip_address = std::string(ip),
            .details = "Login attempt on locked account"
        };
        audit_events_.push_back(ev);
        return Result<User>::Failure(Status::ResourceExhausted("Account is temporarily locked due to consecutive failed attempts"));
    }

    // Verify password
    if (!Crypto::VerifyPassword(password, user.password_hash)) {
        user.failed_attempts++;
        if (user.failed_attempts >= 5) {
            user.locked_until_ts = now + 900; // 15 minute lockout
            user.status = "LOCKED";
            AuditEvent ev{
                .id = Crypto::GenerateUUID(),
                .timestamp_ts = now,
                .actor_id = user.id,
                .actor_type = "USER",
                .org_id = "",
                .event_type = "AUTH_LOCKOUT",
                .target_resource = email_str,
                .outcome = "DENIED",
                .ip_address = std::string(ip),
                .details = "Account locked after 5 consecutive failed login attempts"
            };
            audit_events_.push_back(ev);
            return Result<User>::Failure(Status::ResourceExhausted("Account locked due to consecutive failed attempts"));
        }

        AuditEvent ev{
            .id = Crypto::GenerateUUID(),
            .timestamp_ts = now,
            .actor_id = user.id,
            .actor_type = "USER",
            .org_id = "",
            .event_type = "AUTH_FAILED",
            .target_resource = email_str,
            .outcome = "DENIED",
            .ip_address = std::string(ip),
            .details = "Invalid password provided"
        };
        audit_events_.push_back(ev);
        return Result<User>::Failure(Status::Unauthorized("Invalid credentials"));
    }

    // Success
    user.failed_attempts = 0;
    user.status = "ACTIVE";
    user.locked_until_ts = 0;

    AuditEvent ev{
        .id = Crypto::GenerateUUID(),
        .timestamp_ts = now,
        .actor_id = user.id,
        .actor_type = "USER",
        .org_id = "",
        .event_type = "AUTH_LOGIN",
        .target_resource = email_str,
        .outcome = "SUCCESS",
        .ip_address = std::string(ip),
        .details = "Password authentication successful"
    };
    audit_events_.push_back(ev);

    return Result<User>::Success(user);
}

Result<security::GeneratedOtp> IdentityManager::CreateOtpChallenge(std::string_view email) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<GeneratedOtp>::Failure(Status::Internal("Identity system unavailable"));

    std::string email_str(email);
    auto it = user_id_by_email_.find(email_str);
    if (it == user_id_by_email_.end()) {
        return Result<GeneratedOtp>::Failure(Status::NotFound("User not found"));
    }

    uint64_t now = CurrentTimestamp();
    auto otp_it = otp_by_email_.find(email_str);
    if (otp_it != otp_by_email_.end() && !otp_it->second.used && now < otp_it->second.expires_at_ts) {
        // Rate limit: cannot request new OTP within 30 seconds of previous
        if (now - (otp_it->second.expires_at_ts - 300) < 30) {
            return Result<GeneratedOtp>::Failure(Status::ResourceExhausted("Please wait before requesting another OTP"));
        }
    }

    GeneratedOtp gen = Crypto::GenerateOtp();
    OtpChallenge challenge{
        .id = Crypto::GenerateUUID(),
        .user_id = it->second,
        .email = email_str,
        .otp_hash = gen.hash,
        .attempts = 0,
        .expires_at_ts = now + 300, // 5 minutes TTL
        .used = false
    };

    otp_by_email_[email_str] = challenge;
    return Result<GeneratedOtp>::Success(gen);
}

Result<User> IdentityManager::VerifyOtp(std::string_view email, std::string_view otp_code, std::string_view ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<User>::Failure(Status::Internal("Identity system unavailable"));

    std::string email_str(email);
    auto it = otp_by_email_.find(email_str);
    if (it == otp_by_email_.end()) {
        return Result<User>::Failure(Status::NotFound("No OTP challenge found"));
    }

    OtpChallenge& challenge = it->second;
    uint64_t now = CurrentTimestamp();

    if (challenge.used) {
        return Result<User>::Failure(Status::Unauthorized("OTP has already been used"));
    }
    if (now > challenge.expires_at_ts) {
        return Result<User>::Failure(Status::Unauthorized("OTP has expired"));
    }
    if (challenge.attempts >= 3) {
        challenge.used = true; // Invalidate after 3 bad attempts
        return Result<User>::Failure(Status::ResourceExhausted("Too many invalid OTP attempts"));
    }

    std::string test_hash = Crypto::Sha256Hex(otp_code);
    if (!Crypto::ConstantTimeCompare(test_hash, challenge.otp_hash)) {
        challenge.attempts++;
        return Result<User>::Failure(Status::Unauthorized("Invalid OTP code"));
    }

    challenge.used = true;
    User& user = users_by_id_[challenge.user_id];
    user.failed_attempts = 0;
    user.status = "ACTIVE";

    AuditEvent ev{
        .id = Crypto::GenerateUUID(),
        .timestamp_ts = now,
        .actor_id = user.id,
        .actor_type = "USER",
        .org_id = "",
        .event_type = "AUTH_LOGIN_OTP",
        .target_resource = email_str,
        .outcome = "SUCCESS",
        .ip_address = std::string(ip),
        .details = "OTP authentication successful"
    };
    audit_events_.push_back(ev);

    return Result<User>::Success(user);
}

Result<security::GeneratedToken> IdentityManager::CreateSession(std::string_view user_id, std::string_view org_id, uint64_t ttl_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<GeneratedToken>::Failure(Status::Internal("Identity system unavailable"));

    std::string u_id(user_id);
    if (!users_by_id_.contains(u_id)) {
        return Result<GeneratedToken>::Failure(Status::NotFound("User not found"));
    }

    std::string o_id(org_id);
    Role assigned_role = Role::DEVELOPER;
    bool found_membership = false;

    // Determine role in org
    for (const auto& m : memberships_) {
        if (m.user_id == u_id && (m.org_id == o_id || o_id.empty())) {
            assigned_role = m.role;
            if (o_id.empty()) o_id = m.org_id;
            found_membership = true;
            break;
        }
    }

    if (!found_membership) {
        return Result<GeneratedToken>::Failure(Status::Unauthorized("User is not a member of the specified organization"));
    }

    GeneratedToken gen = Crypto::GenerateSessionToken();
    uint64_t now = CurrentTimestamp();

    Session sess{
        .id = Crypto::GenerateUUID(),
        .token_hash = gen.hash,
        .user_id = u_id,
        .org_id = o_id,
        .role = assigned_role,
        .expires_at_ts = now + ttl_seconds,
        .created_at_ts = now,
        .last_active_ts = now,
        .revoked = false
    };

    sessions_by_hash_[gen.hash] = sess;
    return Result<GeneratedToken>::Success(gen);
}

Result<TenantContext> IdentityManager::AuthenticateSession(std::string_view token, std::string_view ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<TenantContext>::Failure(Status::Internal("Identity system unavailable"));

    std::string token_hash = Crypto::Sha256Hex(token);
    auto it = sessions_by_hash_.find(token_hash);
    if (it == sessions_by_hash_.end() || it->second.revoked) {
        return Result<TenantContext>::Failure(Status::Unauthorized("Invalid or revoked session token"));
    }

    Session& sess = it->second;
    uint64_t now = CurrentTimestamp();

    if (now > sess.expires_at_ts) {
        return Result<TenantContext>::Failure(Status::Unauthorized("Session has expired"));
    }

    sess.last_active_ts = now;

    TenantContext ctx{
        .user_id = sess.user_id,
        .org_id = sess.org_id,
        .role = sess.role,
        .session_id = sess.id,
        .actor_type = "USER",
        .is_platform_admin = (sess.role == Role::PLATFORM_ADMIN),
        .is_authenticated = true
    };

    return Result<TenantContext>::Success(ctx);
}

Status IdentityManager::RevokeSession(std::string_view session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [hash, sess] : sessions_by_hash_) {
        if (sess.id == session_id) {
            sess.revoked = true;
            return Status::Ok();
        }
    }
    return Status::NotFound("Session not found");
}

Status IdentityManager::RevokeAllUserSessions(std::string_view user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [hash, sess] : sessions_by_hash_) {
        if (sess.user_id == user_id) {
            sess.revoked = true;
        }
    }
    return Status::Ok();
}

Result<security::GeneratedApiKey> IdentityManager::CreateApiKey(std::string_view org_id, std::string_view actor_id,
                                                     std::string_view name, Role role, uint64_t ttl_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<GeneratedApiKey>::Failure(Status::Internal("Identity system unavailable"));

    GeneratedApiKey gen = Crypto::GenerateApiKey();
    uint64_t now = CurrentTimestamp();

    ApiKey key{
        .id = Crypto::GenerateUUID(),
        .name = std::string(name),
        .key_prefix = gen.prefix,
        .key_hash = gen.hash,
        .org_id = std::string(org_id),
        .actor_id = std::string(actor_id),
        .role = role,
        .expires_at_ts = ttl_seconds > 0 ? now + ttl_seconds : 0,
        .revoked = false,
        .created_at_ts = now
    };

    api_keys_by_hash_[gen.hash] = key;
    return Result<GeneratedApiKey>::Success(gen);
}

Result<TenantContext> IdentityManager::AuthenticateApiKey(std::string_view api_key, std::string_view ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<TenantContext>::Failure(Status::Internal("Identity system unavailable"));

    std::string key_hash = Crypto::Sha256Hex(api_key);
    auto it = api_keys_by_hash_.find(key_hash);
    if (it == api_keys_by_hash_.end() || it->second.revoked) {
        return Result<TenantContext>::Failure(Status::Unauthorized("Invalid or revoked API key"));
    }

    const ApiKey& key = it->second;
    uint64_t now = CurrentTimestamp();

    if (key.expires_at_ts > 0 && now > key.expires_at_ts) {
        return Result<TenantContext>::Failure(Status::Unauthorized("API key has expired"));
    }

    TenantContext ctx{
        .user_id = key.actor_id,
        .org_id = key.org_id,
        .role = key.role,
        .session_id = key.id,
        .actor_type = "API_KEY",
        .is_platform_admin = (key.role == Role::PLATFORM_ADMIN),
        .is_authenticated = true
    };

    return Result<TenantContext>::Success(ctx);
}

Status IdentityManager::RevokeApiKey(std::string_view key_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [hash, key] : api_keys_by_hash_) {
        if (key.id == key_id) {
            key.revoked = true;
            return Status::Ok();
        }
    }
    return Status::NotFound("API key not found");
}

Result<Organization> IdentityManager::CreateOrganization(const TenantContext& caller, std::string_view name, std::string_view slug) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<Organization>::Failure(Status::Internal("Identity system unavailable"));

    std::string slug_str(slug);
    if (org_id_by_slug_.contains(slug_str)) {
        return Result<Organization>::Failure(Status::AlreadyExists("Organization slug already exists"));
    }

    std::string org_id = "org_" + Crypto::GenerateUUID();
    uint64_t now = CurrentTimestamp();

    Organization org{
        .id = org_id,
        .name = std::string(name),
        .slug = slug_str,
        .status = "ACTIVE",
        .max_databases = 10,
        .max_storage_bytes = 10737418240ULL,
        .max_queries_per_min = 6000,
        .created_at_ts = now
    };

    orgs_by_id_[org_id] = org;
    org_id_by_slug_[slug_str] = org_id;

    // Automatically make creator ORG_OWNER
    if (caller.is_authenticated) {
        Membership m{
            .id = Crypto::GenerateUUID(),
            .org_id = org_id,
            .user_id = caller.user_id,
            .role = Role::ORG_OWNER,
            .status = "ACTIVE",
            .joined_at_ts = now
        };
        memberships_.push_back(m);
    }

    return Result<Organization>::Success(org);
}

Result<Organization> IdentityManager::GetOrganization(std::string_view org_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orgs_by_id_.find(std::string(org_id));
    if (it == orgs_by_id_.end()) {
        return Result<Organization>::Failure(Status::NotFound("Organization not found"));
    }
    return Result<Organization>::Success(it->second);
}

Status IdentityManager::AddMember(const TenantContext& caller, std::string_view org_id, std::string_view user_id, Role role) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Status::Internal("Identity system unavailable");

    if (!caller.is_platform_admin) {
        if (caller.org_id != org_id) return Status::Unauthorized("Cross-organization management denied");
        if (!caller.Can(Permission::MEMBER_INVITE)) return Status::PermissionDenied("Insufficient permissions to invite members");
    }

    std::string u_id(user_id);
    std::string o_id(org_id);
    if (!users_by_id_.contains(u_id)) return Status::NotFound("User not found");
    if (!orgs_by_id_.contains(o_id)) return Status::NotFound("Organization not found");

    for (auto& m : memberships_) {
        if (m.org_id == o_id && m.user_id == u_id) {
            m.role = role;
            return Status::Ok();
        }
    }

    Membership m{
        .id = Crypto::GenerateUUID(),
        .org_id = o_id,
        .user_id = u_id,
        .role = role,
        .status = "ACTIVE",
        .joined_at_ts = CurrentTimestamp()
    };
    memberships_.push_back(m);
    return Status::Ok();
}

Status IdentityManager::RemoveMember(const TenantContext& caller, std::string_view org_id, std::string_view user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Status::Internal("Identity system unavailable");

    if (!caller.is_platform_admin) {
        if (caller.org_id != org_id) return Status::Unauthorized("Cross-organization management denied");
        if (!caller.Can(Permission::MEMBER_REMOVE)) return Status::PermissionDenied("Insufficient permissions to remove members");
    }

    auto it = std::remove_if(memberships_.begin(), memberships_.end(),
                             [&](const Membership& m) {
                                 return m.org_id == org_id && m.user_id == user_id;
                             });
    if (it != memberships_.end()) {
        memberships_.erase(it, memberships_.end());
        return Status::Ok();
    }
    return Status::NotFound("Membership not found");
}

Status IdentityManager::TransferOwnership(const TenantContext& caller, std::string_view org_id, std::string_view new_owner_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Status::Internal("Identity system unavailable");

    if (!caller.is_platform_admin && (caller.org_id != org_id || caller.role != Role::ORG_OWNER)) {
        return Status::PermissionDenied("Only the organization owner or platform admin can transfer ownership");
    }

    bool found_target = false;
    for (auto& m : memberships_) {
        if (m.org_id == org_id) {
            if (m.user_id == new_owner_id) {
                m.role = Role::ORG_OWNER;
                found_target = true;
            } else if (m.user_id == caller.user_id) {
                m.role = Role::ORG_ADMIN; // Demote previous owner to admin
            }
        }
    }
    if (!found_target) {
        return Status::NotFound("Target user is not a member of this organization");
    }
    return Status::Ok();
}

std::vector<Membership> IdentityManager::GetUserMemberships(std::string_view user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Membership> result;
    for (const auto& m : memberships_) {
        if (m.user_id == user_id) {
            result.push_back(m);
        }
    }
    return result;
}

Status IdentityManager::RegisterDatabase(const TenantContext& caller, std::string_view db_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Status::Internal("Identity system unavailable");

    if (IsSystemDatabase(db_name)) {
        return Status::InvalidArgument("Direct registration of system database is forbidden");
    }

    if (!caller.Can(Permission::DATABASE_CREATE)) {
        return Status::PermissionDenied("Insufficient permissions to create database");
    }

    std::string name_str(db_name);
    auto it = db_assignments_.find(name_str);
    if (it != db_assignments_.end()) {
        if (it->second.org_id == caller.org_id) {
            return Status::Ok(); // Already owned by this org
        }
        return Status::AlreadyExists("Database already registered to another tenant");
    }

    // Check organization database quota
    auto org_it = orgs_by_id_.find(caller.org_id);
    if (org_it != orgs_by_id_.end()) {
        size_t count = 0;
        for (const auto& [n, a] : db_assignments_) {
            if (a.org_id == caller.org_id) count++;
        }
        if (count >= org_it->second.max_databases) {
            return Status::ResourceExhausted("Organization has reached its maximum database quota limit");
        }
    }

    DatabaseAssignment assign{
        .db_name = name_str,
        .org_id = caller.org_id,
        .created_at_ts = CurrentTimestamp()
    };
    db_assignments_[name_str] = assign;

    AuditEvent ev{
        .id = Crypto::GenerateUUID(),
        .timestamp_ts = CurrentTimestamp(),
        .actor_id = caller.user_id,
        .actor_type = caller.actor_type,
        .org_id = caller.org_id,
        .event_type = "DB_CREATE",
        .target_resource = name_str,
        .outcome = "SUCCESS",
        .ip_address = "127.0.0.1",
        .details = "Registered database for tenant"
    };
    audit_events_.push_back(ev);

    return Status::Ok();
}

Status IdentityManager::AuthorizeDatabaseAccess(const TenantContext& caller, std::string_view db_name, Permission perm) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Status::Internal("Identity system unavailable: fail closed");

    if (IsSystemDatabase(db_name)) {
        return Status::PermissionDenied("Direct access to internal system database is strictly forbidden");
    }

    if (caller.is_platform_admin) {
        return Status::Ok();
    }

    std::string name_str(db_name);
    auto it = db_assignments_.find(name_str);
    if (it == db_assignments_.end()) {
        return Status::NotFound("Database not found or unassigned to any tenant");
    }

    // Cross-tenant isolation enforcement
    if (it->second.org_id != caller.org_id) {
        AuditEvent ev{
            .id = Crypto::GenerateUUID(),
            .timestamp_ts = CurrentTimestamp(),
            .actor_id = caller.user_id,
            .actor_type = caller.actor_type,
            .org_id = caller.org_id,
            .event_type = "CROSS_TENANT_VIOLATION",
            .target_resource = name_str,
            .outcome = "DENIED",
            .ip_address = "127.0.0.1",
            .details = "Tenant attempted cross-organization database access"
        };
        audit_events_.push_back(ev);
        return Status::Unauthorized("Tenant isolation violation: database does not belong to your organization");
    }

    // Role-based permission check
    if (!caller.Can(perm)) {
        AuditEvent ev{
            .id = Crypto::GenerateUUID(),
            .timestamp_ts = CurrentTimestamp(),
            .actor_id = caller.user_id,
            .actor_type = caller.actor_type,
            .org_id = caller.org_id,
            .event_type = "PERMISSION_DENIED",
            .target_resource = name_str,
            .outcome = "DENIED",
            .ip_address = "127.0.0.1",
            .details = "Caller role does not possess required permission"
        };
        audit_events_.push_back(ev);
        return Status::PermissionDenied("Insufficient permissions for requested operation");
    }

    return Status::Ok();
}

Result<std::vector<std::string>> IdentityManager::ListDatabases(const TenantContext& caller) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<std::vector<std::string>>::Failure(Status::Internal("Identity system unavailable"));

    std::vector<std::string> dbs;
    for (const auto& [name, assign] : db_assignments_) {
        if (caller.is_platform_admin || assign.org_id == caller.org_id) {
            dbs.push_back(name);
        }
    }
    return Result<std::vector<std::string>>::Success(dbs);
}

void IdentityManager::LogAudit(AuditEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.id.empty()) event.id = Crypto::GenerateUUID();
    if (event.timestamp_ts == 0) event.timestamp_ts = CurrentTimestamp();
    audit_events_.push_back(std::move(event));
}

Result<std::vector<AuditEvent>> IdentityManager::GetAuditLogs(const TenantContext& caller, std::string_view org_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<std::vector<AuditEvent>>::Failure(Status::Internal("Identity system unavailable"));

    if (!caller.is_platform_admin && (caller.org_id != org_id || !caller.Can(Permission::AUDIT_VIEW))) {
        return Result<std::vector<AuditEvent>>::Failure(Status::PermissionDenied("Insufficient permissions to view audit logs"));
    }

    std::vector<AuditEvent> filtered;
    for (const auto& ev : audit_events_) {
        if (caller.is_platform_admin || ev.org_id == org_id) {
            filtered.push_back(ev);
        }
    }
    return Result<std::vector<AuditEvent>>::Success(filtered);
}

Result<ServiceAccount> IdentityManager::CreateServiceAccount(const TenantContext& caller, std::string_view org_id,
                                                            std::string_view name, Role role) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<ServiceAccount>::Failure(Status::Internal("Identity system unavailable"));

    if (!caller.is_platform_admin && (caller.org_id != org_id || !caller.Can(Permission::USER_MANAGE))) {
        return Result<ServiceAccount>::Failure(Status::PermissionDenied("Insufficient permissions to create service account"));
    }

    ServiceAccount sa{
        .id = "sa_" + Crypto::GenerateUUID(),
        .org_id = std::string(org_id),
        .name = std::string(name),
        .role = role,
        .created_at_ts = CurrentTimestamp()
    };
    service_accounts_.push_back(sa);
    return Result<ServiceAccount>::Success(sa);
}

Result<AgentIdentity> IdentityManager::CreateAgentIdentity(const TenantContext& caller, std::string_view org_id,
                                                          std::string_view name, std::string_view capabilities,
                                                          std::string_view model_id, Role role) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!operational_) return Result<AgentIdentity>::Failure(Status::Internal("Identity system unavailable"));

    if (!caller.is_platform_admin && (caller.org_id != org_id || !caller.Can(Permission::USER_MANAGE))) {
        return Result<AgentIdentity>::Failure(Status::PermissionDenied("Insufficient permissions to create agent identity"));
    }

    AgentIdentity agent{
        .id = "agent_" + Crypto::GenerateUUID(),
        .org_id = std::string(org_id),
        .name = std::string(name),
        .capabilities = std::string(capabilities),
        .model_id = std::string(model_id),
        .role = role,
        .created_at_ts = CurrentTimestamp()
    };
    agent_identities_.push_back(agent);
    return Result<AgentIdentity>::Success(agent);
}

} // namespace ceebase::identity
