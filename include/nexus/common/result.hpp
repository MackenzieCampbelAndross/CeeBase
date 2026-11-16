#pragma once

#include <cassert>
#include <stdexcept>
#include <utility>
#include <variant>

#include "nexus/common/status.hpp"

namespace nexus {

template <typename T>
class [[nodiscard]] Result {
public:
    // Successful construction
    Result(const T& val) : storage_(val) {}
    Result(T&& val) noexcept(std::is_nothrow_move_constructible_v<T>) : storage_(std::move(val)) {}

    // Failure construction
    Result(const Status& status) : storage_(status) {
        assert(!status.ok() && "Result failure constructor cannot accept Status::Ok");
    }
    Result(Status&& status) noexcept : storage_(std::move(status)) {
        assert(!std::get<Status>(storage_).ok() && "Result failure constructor cannot accept Status::Ok");
    }

    [[nodiscard]] bool ok() const noexcept {
        return std::holds_alternative<T>(storage_);
    }

    [[nodiscard]] const Status& status() const noexcept {
        static const Status ok_status = Status::Ok();
        if (ok()) {
            return ok_status;
        }
        return std::get<Status>(storage_);
    }

    [[nodiscard]] T& value() & {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value on failed Result: " + status().ToString());
        }
        return std::get<T>(storage_);
    }

    [[nodiscard]] const T& value() const & {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value on failed Result: " + status().ToString());
        }
        return std::get<T>(storage_);
    }

    [[nodiscard]] T&& value() && {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value on failed Result: " + status().ToString());
        }
        return std::get<T>(std::move(storage_));
    }

    [[nodiscard]] T* operator->() {
        return &value();
    }

    [[nodiscard]] const T* operator->() const {
        return &value();
    }

    [[nodiscard]] T& operator*() & {
        return value();
    }

    [[nodiscard]] const T& operator*() const & {
        return value();
    }

    [[nodiscard]] T&& operator*() && {
        return std::move(value());
    }

private:
    std::variant<T, Status> storage_;
};

} // namespace nexus
