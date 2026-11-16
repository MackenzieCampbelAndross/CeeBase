#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ceebase/common/types.hpp"
#include "ceebase/record/schema.hpp"
#include "ceebase/record/value.hpp"

namespace ceebase {

class Tuple {
public:
    Tuple() = default;
    Tuple(const std::vector<Value>& values, const Schema& schema);
    explicit Tuple(std::vector<uint8_t> data, RID rid = {}) : data_(std::move(data)), rid_(rid) {}
    Tuple(std::span<const uint8_t> data, RID rid = {});

    [[nodiscard]] Value GetValue(const Schema& schema, size_t col_idx) const;

    [[nodiscard]] const uint8_t* GetData() const noexcept { return data_.data(); }
    [[nodiscard]] size_t GetLength() const noexcept { return data_.size(); }
    [[nodiscard]] const std::vector<uint8_t>& GetDataVector() const noexcept { return data_; }
    [[nodiscard]] RID GetRID() const noexcept { return rid_; }
    void SetRID(const RID& rid) noexcept { rid_ = rid; }

    [[nodiscard]] bool IsNull(const Schema& schema, size_t col_idx) const noexcept;
    [[nodiscard]] std::string ToString(const Schema& schema) const;

private:
    std::vector<uint8_t> data_{};
    RID rid_{};
};

} // namespace ceebase
