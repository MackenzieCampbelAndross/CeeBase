#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

namespace ceebase::identity {

enum class Role {
    PLATFORM_ADMIN,
    ORG_OWNER,
    ORG_ADMIN,
    DB_ADMIN,
    DEVELOPER,
    READ_ONLY_ANALYST,
    SERVICE_ACCOUNT,
    AI_AGENT
};

inline std::string RoleToString(Role role) {
    switch (role) {
        case Role::PLATFORM_ADMIN: return "PLATFORM_ADMIN";
        case Role::ORG_OWNER: return "ORG_OWNER";
        case Role::ORG_ADMIN: return "ORG_ADMIN";
        case Role::DB_ADMIN: return "DB_ADMIN";
        case Role::DEVELOPER: return "DEVELOPER";
        case Role::READ_ONLY_ANALYST: return "READ_ONLY_ANALYST";
        case Role::SERVICE_ACCOUNT: return "SERVICE_ACCOUNT";
        case Role::AI_AGENT: return "AI_AGENT";
    }
    return "DEVELOPER";
}

inline Role StringToRole(std::string_view str) {
    if (str == "PLATFORM_ADMIN") return Role::PLATFORM_ADMIN;
    if (str == "ORG_OWNER") return Role::ORG_OWNER;
    if (str == "ORG_ADMIN") return Role::ORG_ADMIN;
    if (str == "DB_ADMIN") return Role::DB_ADMIN;
    if (str == "DEVELOPER") return Role::DEVELOPER;
    if (str == "READ_ONLY_ANALYST") return Role::READ_ONLY_ANALYST;
    if (str == "SERVICE_ACCOUNT") return Role::SERVICE_ACCOUNT;
    if (str == "AI_AGENT") return Role::AI_AGENT;
    return Role::DEVELOPER;
}

enum class Permission {
    DATABASE_CREATE,
    DATABASE_DROP,
    DATABASE_LIST,
    DATABASE_CONNECT,
    SCHEMA_ALTER,
    TABLE_CREATE,
    TABLE_DROP,
    DATA_READ,
    DATA_INSERT,
    DATA_UPDATE,
    DATA_DELETE,
    INDEX_MANAGE,
    BACKUP_CREATE,
    BACKUP_RESTORE,
    USER_MANAGE,
    MEMBER_INVITE,
    MEMBER_REMOVE,
    ROLE_ASSIGN,
    SECURITY_ADMIN,
    API_KEY_MANAGE,
    AUDIT_VIEW
};

inline bool HasPermission(Role role, Permission perm) noexcept {
    // Platform Administrator has total system authority
    if (role == Role::PLATFORM_ADMIN) {
        return true;
    }

    switch (perm) {
        case Permission::DATABASE_CREATE:
        case Permission::DATABASE_DROP:
            return role == Role::ORG_OWNER || role == Role::ORG_ADMIN;

        case Permission::DATABASE_LIST:
        case Permission::DATABASE_CONNECT:
            return true; // Any authenticated member can list/connect to their own org databases

        case Permission::SCHEMA_ALTER:
        case Permission::TABLE_CREATE:
        case Permission::TABLE_DROP:
        case Permission::INDEX_MANAGE:
            return role == Role::ORG_OWNER || role == Role::ORG_ADMIN ||
                   role == Role::DB_ADMIN || role == Role::DEVELOPER;

        case Permission::DATA_READ:
            return true; // All roles can read

        case Permission::DATA_INSERT:
        case Permission::DATA_UPDATE:
        case Permission::DATA_DELETE:
            return role != Role::READ_ONLY_ANALYST;

        case Permission::BACKUP_CREATE:
            return role == Role::ORG_OWNER || role == Role::ORG_ADMIN || role == Role::DB_ADMIN;

        case Permission::BACKUP_RESTORE:
            return role == Role::ORG_OWNER || role == Role::ORG_ADMIN;

        case Permission::USER_MANAGE:
        case Permission::MEMBER_INVITE:
        case Permission::MEMBER_REMOVE:
        case Permission::ROLE_ASSIGN:
        case Permission::SECURITY_ADMIN:
            return role == Role::ORG_OWNER || role == Role::ORG_ADMIN;

        case Permission::API_KEY_MANAGE:
            return role == Role::ORG_OWNER || role == Role::ORG_ADMIN || role == Role::DEVELOPER;

        case Permission::AUDIT_VIEW:
            return role == Role::ORG_OWNER || role == Role::ORG_ADMIN;
    }
    return false;
}

struct User {
    std::string id;
    std::string email;
    std::string password_hash;
    std::string status{"ACTIVE"}; // ACTIVE, LOCKED, SUSPENDED
    uint32_t failed_attempts{0};
    uint64_t locked_until_ts{0};
    uint64_t created_at_ts{0};
    uint64_t updated_at_ts{0};
};

struct Organization {
    std::string id;
    std::string name;
    std::string slug;
    std::string status{"ACTIVE"}; // ACTIVE, SUSPENDED
    uint32_t max_databases{10};
    uint64_t max_storage_bytes{10737418240ULL}; // 10 GB default
    uint32_t max_queries_per_min{6000};
    uint64_t created_at_ts{0};
};

struct Membership {
    std::string id;
    std::string org_id;
    std::string user_id;
    Role role{Role::DEVELOPER};
    std::string status{"ACTIVE"}; // ACTIVE, INVITED
    uint64_t joined_at_ts{0};
};

struct Session {
    std::string id;
    std::string token_hash;
    std::string user_id;
    std::string org_id;
    Role role{Role::DEVELOPER};
    uint64_t expires_at_ts{0};
    uint64_t created_at_ts{0};
    uint64_t last_active_ts{0};
    bool revoked{false};
};

struct ApiKey {
    std::string id;
    std::string name;
    std::string key_prefix;
    std::string key_hash;
    std::string org_id;
    std::string actor_id;
    Role role{Role::DEVELOPER};
    uint64_t expires_at_ts{0};
    bool revoked{false};
    uint64_t created_at_ts{0};
};

struct ServiceAccount {
    std::string id;
    std::string org_id;
    std::string name;
    Role role{Role::SERVICE_ACCOUNT};
    uint64_t created_at_ts{0};
};

struct AgentIdentity {
    std::string id;
    std::string org_id;
    std::string name;
    std::string capabilities;
    std::string model_id;
    Role role{Role::AI_AGENT};
    uint64_t created_at_ts{0};
};

struct AuditEvent {
    std::string id;
    uint64_t timestamp_ts{0};
    std::string actor_id;
    std::string actor_type{"USER"}; // USER, SERVICE_ACCOUNT, AI_AGENT, BREAK_GLASS, SYSTEM
    std::string org_id;
    std::string event_type;         // AUTH_LOGIN, AUTH_LOGOUT, AUTH_FAILED, AUTH_LOCKOUT, DB_CREATE, DB_DROP, PERMISSION_DENIED
    std::string target_resource;
    std::string outcome{"SUCCESS"}; // SUCCESS, DENIED, FAILED
    std::string ip_address{"127.0.0.1"};
    std::string details;
};

struct OtpChallenge {
    std::string id;
    std::string user_id;
    std::string email;
    std::string otp_hash;
    uint32_t attempts{0};
    uint64_t expires_at_ts{0};
    bool used{false};
};

struct DatabaseAssignment {
    std::string db_name;
    std::string org_id;
    uint64_t created_at_ts{0};
};

struct TenantContext {
    std::string user_id{};
    std::string org_id{};
    Role role{Role::READ_ONLY_ANALYST};
    std::string session_id{};
    std::string actor_type{"USER"};
    bool is_platform_admin{false};
    bool is_authenticated{false};

    [[nodiscard]] bool Can(Permission perm) const noexcept {
        if (!is_authenticated) return false;
        if (is_platform_admin) return true;
        return HasPermission(role, perm);
    }
};

} // namespace ceebase::identity
