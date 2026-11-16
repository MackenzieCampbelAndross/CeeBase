#pragma once

#include <cstdio>
#include <string>

#include "nexus/common/status.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <cerrno>
#include <cstring>
#include <unistd.h>
#endif

namespace nexus::os {

// Synchronize a C FILE pointer with underlying physical disk media
inline Status SyncFile(FILE* fp) {
    if (!fp) {
        return Status::InvalidArgument("os::SyncFile: null file pointer");
    }

    // Flush C runtime stream buffer first
    if (std::fflush(fp) != 0) {
        return Status::IoError("os::SyncFile: fflush failed");
    }

#ifdef _WIN32
    int fd = _fileno(fp);
    if (fd < 0) {
        return Status::IoError("os::SyncFile: _fileno failed");
    }

    HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (handle == INVALID_HANDLE_VALUE) {
        return Status::IoError("os::SyncFile: _get_osfhandle failed");
    }

    if (!FlushFileBuffers(handle)) {
        DWORD err = GetLastError();
        return Status::IoError("os::SyncFile: FlushFileBuffers failed with Windows error: " + std::to_string(err));
    }
#else
    int fd = fileno(fp);
    if (fd < 0) {
        return Status::IoError("os::SyncFile: fileno failed");
    }

    if (fsync(fd) != 0) {
        return Status::IoError("os::SyncFile: fsync failed: " + std::string(std::strerror(errno)));
    }
#endif

    return Status::Ok();
}

// Seek 64-bit offset safely across platforms
inline int Seek64(FILE* fp, int64_t offset, int origin) {
#ifdef _WIN32
    return _fseeki64(fp, offset, origin);
#else
    return fseeko(fp, offset, origin);
#endif
}

// Tell 64-bit offset safely across platforms
inline int64_t Tell64(FILE* fp) {
#ifdef _WIN32
    return _ftelli64(fp);
#else
    return ftello(fp);
#endif
}

} // namespace nexus::os
