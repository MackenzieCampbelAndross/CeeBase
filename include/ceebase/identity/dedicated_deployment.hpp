#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ceebase/common/result.hpp"
#include "ceebase/common/status.hpp"

namespace ceebase::identity {

struct DedicatedDeploymentConfig {
    std::string org_id;
    std::string deployment_name;
    std::string data_directory;
    uint16_t port{8085};
    std::string status{"ACTIVE"}; // ACTIVE, STOPPED, PROVISIONING
    uint64_t created_at_ts{0};
};

class DedicatedDeploymentManager {
public:
    DedicatedDeploymentManager() = default;

    Result<DedicatedDeploymentConfig> ProvisionDeployment(const std::string& org_id,
                                                         const std::string& deployment_name,
                                                         const std::string& base_storage_dir,
                                                         uint16_t port);

    Result<DedicatedDeploymentConfig> GetDeployment(const std::string& org_id) const;
    std::vector<DedicatedDeploymentConfig> ListDeployments() const;

private:
    std::vector<DedicatedDeploymentConfig> deployments_;
};

} // namespace ceebase::identity
