#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "nexus/common/types.hpp"

namespace nexus {

class Value {
public:
    Value() : type_(DataType::INVALID), is_null_(true) {}
    explicit Value(DataType type) : type_(type), is_null_(true) {}
    explicit Value(int32_t val) : type_(DataType::INTEGER), is_null_(false), val_(val) {}
    explicit Value(int64_t val) : type_(DataType::BIGINT), is_null_(false), val_(val) {}
    explicit Value(float val) : type_(DataType::FLOAT), is_null_(false), val_(val) {}
    explicit Value(double val) : type_(DataType::DOUBLE), is_null_(false), val_(val) {}
    explicit Value(bool val) : type_(DataType::BOOLEAN), is_null_(false), val_(val) {}
    explicit Value(std::string val) : type_(DataType::TEXT), is_null_(false), val_(std::move(val)) {}
    explicit Value(std::string_view val) : type_(DataType::TEXT), is_null_(false), val_(std::string(val)) {}
    explicit Value(const char* val) : type_(DataType::TEXT), is_null_(false), val_(std::string(val)) {}

    [[nodiscard]] DataType GetType() const noexcept { return type_; }
    [[nodiscard]] bool IsNull() const noexcept { return is_null_; }

    [[nodiscard]] int32_t GetInteger() const;
    [[nodiscard]] int64_t GetBigInt() const;
    [[nodiscard]] float GetFloat() const;
    [[nodiscard]] double GetDouble() const;
    [[nodiscard]] bool GetBoolean() const;
    [[nodiscard]] const std::string& GetText() const;

    [[nodiscard]] std::string ToString() const;

    // Comparisons
    [[nodiscard]] bool operator==(const Value& other) const;
    [[nodiscard]] bool operator!=(const Value& other) const { return !(*this == other); }
    [[nodiscard]] bool operator<(const Value& other) const;
    [[nodiscard]] bool operator<=(const Value& other) const { return *this < other || *this == other; }
    [[nodiscard]] bool operator>(const Value& other) const { return !(*this <= other); }
    [[nodiscard]] bool operator>=(const Value& other) const { return !(*this < other); }

    // Static factories
    static Value MakeNull(DataType type) { return Value(type); }

private:
    DataType type_{DataType::INVALID};
    bool is_null_{true};
    std::variant<std::monostate, int32_t, int64_t, float, double, bool, std::string> val_{};
};

} // namespace nexus
