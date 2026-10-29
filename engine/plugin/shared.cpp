// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "plugin/shared.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <new>
#include <utility>

namespace rb::plugin
{

bool pushParam(SharedBlock &shared, const ParamChange &change) noexcept
{
    const std::uint64_t write = shared.paramWrite.load(std::memory_order_relaxed);
    const std::uint64_t read = shared.paramRead.load(std::memory_order_acquire);
    if (write - read >= kParamSlots) {
        shared.paramsDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    shared.params[write % kParamSlots] = change;
    shared.paramWrite.store(write + 1, std::memory_order_release);
    return true;
}

bool popParam(SharedBlock &shared, ParamChange &change) noexcept RB_NONBLOCKING
{
    const std::uint64_t read = shared.paramRead.load(std::memory_order_relaxed);
    if (read == shared.paramWrite.load(std::memory_order_acquire)) {
        return false;
    }
    change = shared.params[read % kParamSlots];
    shared.paramRead.store(read + 1, std::memory_order_release);
    return true;
}

std::expected<SharedMemory, SharedError> SharedMemory::create()
{
    const int fd = ::memfd_create("reverblabr-plugin", MFD_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(SharedError::NotCreated);
    }
    if (::ftruncate(fd, sizeof(SharedBlock)) != 0) {
        ::close(fd);
        return std::unexpected(SharedError::NotCreated);
    }
    void *memory = ::mmap(nullptr, sizeof(SharedBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(SharedError::NotCreated);
    }
    return SharedMemory(fd, new (memory) SharedBlock);
}

std::expected<SharedMemory, SharedError> SharedMemory::map(int fd)
{
    struct stat info{};
    if (::fstat(fd, &info) != 0 || static_cast<std::size_t>(info.st_size) < sizeof(SharedBlock)) {
        ::close(fd);
        return std::unexpected(SharedError::NotMapped);
    }
    void *memory = ::mmap(nullptr, sizeof(SharedBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(SharedError::NotMapped);
    }
    auto *block = static_cast<SharedBlock *>(memory);
    if (block->magic != kSharedMagic || block->version != kProtocolVersion) {
        ::munmap(memory, sizeof(SharedBlock));
        ::close(fd);
        return std::unexpected(SharedError::WrongLayout);
    }
    return SharedMemory(fd, block);
}

SharedMemory::SharedMemory(SharedMemory &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1))
    , m_block(std::exchange(other.m_block, nullptr))
{
}

SharedMemory &SharedMemory::operator=(SharedMemory &&other) noexcept
{
    if (this != &other) {
        this->~SharedMemory();
        m_fd = std::exchange(other.m_fd, -1);
        m_block = std::exchange(other.m_block, nullptr);
    }
    return *this;
}

SharedMemory::~SharedMemory()
{
    if (m_block != nullptr) {
        ::munmap(m_block, sizeof(SharedBlock));
    }
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

} // namespace rb::plugin
