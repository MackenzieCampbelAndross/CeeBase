#include "ceebase/common/status.hpp"

namespace ceebase {

std::string_view StatusCodeToString(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Ok:
            return "OK";
        case StatusCode::NotFound:
            return "NOT_FOUND";
        case StatusCode::AlreadyExists:
            return "ALREADY_EXISTS";
        case StatusCode::InvalidArgument:
            return "INVALID_ARGUMENT";
        case StatusCode::Corruption:
            return "CORRUPTION";
        case StatusCode::IoError:
            return "IO_ERROR";
        case StatusCode::OutOfMemory:
            return "OUT_OF_MEMORY";
        case StatusCode::OutOfRange:
            return "OUT_OF_RANGE";
        case StatusCode::BufferPoolExhausted:
            return "BUFFER_POOL_EXHAUSTED";
        case StatusCode::NotSupported:
            return "NOT_SUPPORTED";
        case StatusCode::Internal:
            return "INTERNAL_ERROR";
        default:
            return "UNKNOWN_STATUS";
    }
}

std::string Status::ToString() const {
    if (ok()) {
        return "OK";
    }
    std::string result(StatusCodeToString(code_));
    if (!message_.empty()) {
        result += ": ";
        result += message_;
    }
    return result;
}

} // namespace ceebase
