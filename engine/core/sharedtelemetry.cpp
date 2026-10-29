// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/sharedtelemetry.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <new>
#include <string>
#include <utility>

namespace rb::core
{

std::expected<SharedTelemetry, SharedTelemetryError> SharedTelemetry::create()
{
    const int fd = ::memfd_create("reverblabr-telemetry", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        return std::unexpected(SharedTelemetryError::NotCreated);
    }
    // **Sealed at its size before anybody else can see it**, so no mapping
    // of it can ever reach past the end of the file.
    if (::ftruncate(fd, sizeof(TelemetryRegion)) != 0
        || ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0) {
        ::close(fd);
        return std::unexpected(SharedTelemetryError::NotCreated);
    }
    void *memory = ::mmap(nullptr, sizeof(TelemetryRegion), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(SharedTelemetryError::NotCreated);
    }
    return SharedTelemetry(fd, new (memory) TelemetryRegion);
}

std::expected<SharedTelemetry, SharedTelemetryError> SharedTelemetry::map(int fd)
{
    struct stat info{};
    if (fd < 0 || ::fstat(fd, &info) != 0 || static_cast<std::size_t>(info.st_size) < sizeof(TelemetryRegion)) {
        if (fd >= 0) {
            ::close(fd);
        }
        return std::unexpected(SharedTelemetryError::NotMapped);
    }
    const int seals = ::fcntl(fd, F_GET_SEALS);
    if (seals < 0 || (seals & F_SEAL_SHRINK) == 0) {
        ::close(fd);
        return std::unexpected(SharedTelemetryError::NotSealed);
    }
    void *memory = ::mmap(nullptr, sizeof(TelemetryRegion), PROT_READ, MAP_SHARED, fd, 0);
    // The mapping keeps the memory; the descriptor has done its job.
    ::close(fd);
    if (memory == MAP_FAILED) {
        return std::unexpected(SharedTelemetryError::NotMapped);
    }
    auto *region = static_cast<TelemetryRegion *>(memory);
    if (region->magic != kTelemetryMagic || region->version != kTelemetryVersion
        || region->layout != telemetryLayout()) {
        ::munmap(memory, sizeof(TelemetryRegion));
        return std::unexpected(SharedTelemetryError::WrongLayout);
    }
    return SharedTelemetry(-1, region);
}

SharedTelemetry::SharedTelemetry(SharedTelemetry &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1))
    , m_region(std::exchange(other.m_region, nullptr))
{
}

SharedTelemetry &SharedTelemetry::operator=(SharedTelemetry &&other) noexcept
{
    if (this != &other) {
        this->~SharedTelemetry();
        m_fd = std::exchange(other.m_fd, -1);
        m_region = std::exchange(other.m_region, nullptr);
    }
    return *this;
}

SharedTelemetry::~SharedTelemetry()
{
    if (m_region != nullptr) {
        if (m_fd >= 0) {
            m_region->~TelemetryRegion();
        }
        ::munmap(m_region, sizeof(TelemetryRegion));
    }
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

PublishedSlot::PublishedSlot()
{
    if (auto created = SharedTelemetry::create(); created) {
        m_shared.emplace(std::move(*created));
    } else {
        m_own = std::make_unique<TelemetrySlot>();
    }
}

int SharedTelemetry::shareReadOnly() const
{
    if (m_fd < 0) {
        return -1;
    }
    // **Reopened, not duplicated**: a dup() would carry this descriptor's
    // write access to whoever it is handed to.
    const std::string path = "/proc/self/fd/" + std::to_string(m_fd);
    return ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
}

} // namespace rb::core
