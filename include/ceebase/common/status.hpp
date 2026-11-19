#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ceebase {

enum class StatusCode : uint8_t {
    Ok = 0,
    NotFound,
    AlreadyExists,
    InvalidArgument,
    Corruption,
    IoError,
    OutOfMemory,
    OutOfRange,
    BufferPoolExhausted,
    NotSupported,
    Unauthorized,
    PermissionDenied,
    ResourceExhausted,
    Internal
};

[[nodiscard]] std::string_view StatusCodeToString(StatusCode code) noexcept;

class [[nodiscard]] Status {
public:
    constexpr Status() noexcept : code_(StatusCode::Ok) {}
    Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

    [[nodiscard]] static Status Ok() noexcept { return Status(); }
    [[nodiscard]] static Status NotFound(std::string message) {
        return Status(StatusCode::NotFound, std::move(message));
    }
    [[nodiscard]] static Status AlreadyExists(std::string message) {
        return Status(StatusCode::AlreadyExists, std::move(message));
    }
    [[nodiscard]] static Status InvalidArgument(std::string message) {
        return Status(StatusCode::InvalidArgument, std::move(message));
    }
    [[nodiscard]] static Status Corruption(std::string message) {
        return Status(StatusCode::Corruption, std::move(message));
    }
    [[nodiscard]] static Status IoError(std::string message) {
        return Status(StatusCode::IoError, std::move(message));
    }
    [[nodiscard]] static Status OutOfMemory(std::string message) {
        return Status(StatusCode::OutOfMemory, std::move(message));
    }
    [[nodiscard]] static Status OutOfRange(std::string message) {
        return Status(StatusCode::OutOfRange, std::move(message));
    }
    [[nodiscard]] static Status BufferPoolExhausted(std::string message) {
        return Status(StatusCode::BufferPoolExhausted, std::move(message));
    }
    [[nodiscard]] static Status NotSupported(std::string message) {
        return Status(StatusCode::NotSupported, std::move(message));
    }
    [[nodiscard]] static Status Unauthorized(std::string message) {
        return Status(StatusCode::Unauthorized, std::move(message));
    }
    [[nodiscard]] static Status PermissionDenied(std::string message) {
        return Status(StatusCode::PermissionDenied, std::move(message));
    }
    [[nodiscard]] static Status ResourceExhausted(std::string message) {
        return Status(StatusCode::ResourceExhausted, std::move(message));
    }
    [[nodiscard]] static Status Internal(std::string message) {
        return Status(StatusCode::Internal, std::move(message));
    }

    [[nodiscard]] constexpr bool ok() const noexcept { return code_ == StatusCode::Ok; }
    [[nodiscard]] constexpr StatusCode code() const noexcept { return code_; }
    [[nodiscard]] constexpr bool IsNotFound() const noexcept { return code_ == StatusCode::NotFound; }
    [[nodiscard]] constexpr bool IsAlreadyExists() const noexcept { return code_ == StatusCode::AlreadyExists; }
    [[nodiscard]] std::string_view message() const noexcept { return message_; }

    [[nodiscard]] std::string ToString() const;

    [[nodiscard]] bool operator==(const Status& other) const noexcept {
        return code_ == other.code_ && message_ == other.message_;
    }

    [[nodiscard]] bool operator!=(const Status& other) const noexcept {
        return !(*this == other);
    }

private:
    StatusCode code_{StatusCode::Ok};
    std::string message_{};
};

} // namespace ceebase
