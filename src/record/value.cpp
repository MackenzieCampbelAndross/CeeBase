#include "nexus/record/value.hpp"

#include <stdexcept>

namespace nexus {

int32_t Value::GetInteger() const {
    if (is_null_ || type_ != DataType::INTEGER) {
        throw std::runtime_error("Value: Type mismatch, expected INTEGER");
    }
    return std::get<int32_t>(val_);
}

int64_t Value::GetBigInt() const {
    if (is_null_ || type_ != DataType::BIGINT) {
        throw std::runtime_error("Value: Type mismatch, expected BIGINT");
    }
    return std::get<int64_t>(val_);
}

float Value::GetFloat() const {
    if (is_null_ || type_ != DataType::FLOAT) {
        throw std::runtime_error("Value: Type mismatch, expected FLOAT");
    }
    return std::get<float>(val_);
}

double Value::GetDouble() const {
    if (is_null_ || type_ != DataType::DOUBLE) {
        throw std::runtime_error("Value: Type mismatch, expected DOUBLE");
    }
    return std::get<double>(val_);
}

bool Value::GetBoolean() const {
    if (is_null_ || type_ != DataType::BOOLEAN) {
        throw std::runtime_error("Value: Type mismatch, expected BOOLEAN");
    }
    return std::get<bool>(val_);
}

const std::string& Value::GetText() const {
    if (is_null_ || type_ != DataType::TEXT) {
        throw std::runtime_error("Value: Type mismatch, expected TEXT");
    }
    return std::get<std::string>(val_);
}

std::string Value::ToString() const {
    if (is_null_) {
        return "NULL";
    }
    switch (type_) {
        case DataType::INTEGER:
            return std::to_string(std::get<int32_t>(val_));
        case DataType::BIGINT:
            return std::to_string(std::get<int64_t>(val_));
        case DataType::FLOAT:
            return std::to_string(std::get<float>(val_));
        case DataType::DOUBLE:
            return std::to_string(std::get<double>(val_));
        case DataType::BOOLEAN:
            return std::get<bool>(val_) ? "true" : "false";
        case DataType::TEXT:
            return std::get<std::string>(val_);
        case DataType::INVALID:
        default:
            return "INVALID";
    }
}

bool Value::operator==(const Value& other) const {
    if (type_ != other.type_) {
        return false;
    }
    if (is_null_ || other.is_null_) {
        return is_null_ && other.is_null_;
    }
    return val_ == other.val_;
}

bool Value::operator<(const Value& other) const {
    if (type_ != other.type_) {
        return type_ < other.type_;
    }
    if (is_null_) {
        return !other.is_null_; // NULLs sort first
    }
    if (other.is_null_) {
        return false;
    }
    return val_ < other.val_;
}

} // namespace nexus
