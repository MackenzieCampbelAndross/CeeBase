#include "ceebase/identity/dedicated_deployment.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace ceebase::identity {

Result<DedicatedDeploymentConfig> DedicatedDeploymentManager::ProvisionDeployment(
    const std::string& org_id,
    const std::string& deployment_name,
    const std::string& base_storage_dir,
    uint16_t port) {

    for (const auto& d : deployments_) {
        if (d.org_id == org_id) {
            return Result<DedicatedDeploymentConfig>::Failure(Status::AlreadyExists("Dedicated deployment already provisioned for this organization"));
        }
        if (d.port == port) {
            return Result<DedicatedDeploymentConfig>::Failure(Status::AlreadyExists("Port already allocated to another dedicated deployment"));
        }
    }

    std::filesystem::path org_path = std::filesystem::path(base_storage_dir) / org_id;
    std::filesystem::path data_dir = org_path / "data";
    std::filesystem::path wal_dir = org_path / "wal";
    std::filesystem::path conf_dir = org_path / "conf";

    std::error_code ec;
    std::filesystem::create_directories(data_dir, ec);
    std::filesystem::create_directories(wal_dir, ec);
    std::filesystem::create_directories(conf_dir, ec);
    if (ec) {
        return Result<DedicatedDeploymentConfig>::Failure(Status::Internal("Failed creating dedicated storage volume directories: " + ec.message()));
    }

    // Write dedicated server configuration
    std::filesystem::path conf_file = conf_dir / "ceebase_dedicated.conf";
    std::ofstream out(conf_file);
    if (!out.is_open()) {
        return Result<DedicatedDeploymentConfig>::Failure(Status::Internal("Failed creating configuration file"));
    }

    out << "# CeeBase Dedicated Deployment Configuration\n";
    out << "[server]\n";
    out << "host = 0.0.0.0\n";
    out << "port = " << port << "\n";
    out << "data_directory = " << data_dir.string() << "\n";
    out << "wal_directory = " << wal_dir.string() << "\n";
    out << "org_id = " << org_id << "\n";
    out << "isolation_level = dedicated\n";
    out << "tls_enforce = true\n";
    out.close();

    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    DedicatedDeploymentConfig config{
        .org_id = org_id,
        .deployment_name = deployment_name,
        .data_directory = data_dir.string(),
        .port = port,
        .status = "ACTIVE",
        .created_at_ts = now
    };

    deployments_.push_back(config);
    return Result<DedicatedDeploymentConfig>::Success(config);
}

Result<DedicatedDeploymentConfig> DedicatedDeploymentManager::GetDeployment(const std::string& org_id) const {
    for (const auto& d : deployments_) {
        if (d.org_id == org_id) {
            return Result<DedicatedDeploymentConfig>::Success(d);
        }
    }
    return Result<DedicatedDeploymentConfig>::Failure(Status::NotFound("Deployment not found for organization"));
}

std::vector<DedicatedDeploymentConfig> DedicatedDeploymentManager::ListDeployments() const {
    return deployments_;
}

} // namespace ceebase::identity
