// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model/shared.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bit>
#include <new>
#include <utility>

namespace rb::model
{

namespace
{

/// The first ring starts here, past the header, on a line of its own.
constexpr std::size_t kRingsStart = (sizeof(SharedHeader) + 63) / 64 * 64;
/// The most frames a ring may hold: more than any latency the block allows.
constexpr std::uint32_t kLongestRing = 1U << 22;

[[nodiscard]] constexpr std::size_t bytesFor(std::uint32_t capacity) noexcept
{
    return kRingsStart + std::size_t{capacity} * kRingChannels * 2 * sizeof(float);
}

} // namespace

std::expected<SharedRings, SharedError> SharedRings::create(std::uint32_t frames)
{
    if (frames == 0 || frames > kLongestRing) {
        return std::unexpected(SharedError::NotCreated);
    }
    const std::uint32_t capacity = std::bit_ceil(frames);
    const std::size_t bytes = bytesFor(capacity);
    const int fd = ::memfd_create("reverblabr-model", MFD_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(SharedError::NotCreated);
    }
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        ::close(fd);
        return std::unexpected(SharedError::NotCreated);
    }
    void *memory = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(SharedError::NotCreated);
    }
    auto *header = new (memory) SharedHeader;
    header->capacity = capacity;
    return SharedRings(fd, memory, bytes, capacity);
}

std::expected<SharedRings, SharedError> SharedRings::map(int fd)
{
    struct stat info{};
    if (::fstat(fd, &info) != 0 || static_cast<std::size_t>(info.st_size) < sizeof(SharedHeader)) {
        ::close(fd);
        return std::unexpected(SharedError::NotMapped);
    }
    const auto bytes = static_cast<std::size_t>(info.st_size);
    void *memory = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(SharedError::NotMapped);
    }
    const auto *header = static_cast<const SharedHeader *>(memory);
    const std::uint32_t capacity = header->capacity;
    if (header->magic != kSharedMagic || header->version != kProtocolVersion || header->channels != kRingChannels
        || !std::has_single_bit(capacity) || capacity > kLongestRing || bytesFor(capacity) != bytes) {
        ::munmap(memory, bytes);
        ::close(fd);
        return std::unexpected(SharedError::WrongLayout);
    }
    return SharedRings(fd, memory, bytes, capacity);
}

std::span<float> SharedRings::input(std::uint32_t channel) const noexcept
{
    auto *first = reinterpret_cast<float *>(static_cast<char *>(m_memory) + kRingsStart);
    return {first + std::size_t{channel} * m_capacity, m_capacity};
}

std::span<float> SharedRings::output(std::uint32_t channel) const noexcept
{
    auto *first = reinterpret_cast<float *>(static_cast<char *>(m_memory) + kRingsStart);
    return {first + std::size_t{kRingChannels + channel} * m_capacity, m_capacity};
}

SharedRings::SharedRings(SharedRings &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1))
    , m_memory(std::exchange(other.m_memory, nullptr))
    , m_bytes(std::exchange(other.m_bytes, 0))
    , m_capacity(std::exchange(other.m_capacity, 0))
{
}

SharedRings &SharedRings::operator=(SharedRings &&other) noexcept
{
    if (this != &other) {
        this->~SharedRings();
        m_fd = std::exchange(other.m_fd, -1);
        m_memory = std::exchange(other.m_memory, nullptr);
        m_bytes = std::exchange(other.m_bytes, 0);
        m_capacity = std::exchange(other.m_capacity, 0);
    }
    return *this;
}

SharedRings::~SharedRings()
{
    if (m_memory != nullptr) {
        ::munmap(m_memory, m_bytes);
    }
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

} // namespace rb::model
