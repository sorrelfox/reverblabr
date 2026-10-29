// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"
#include "rt/spscqueue.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rb::pluginhost
{

/// **Atom messages between an LV2 plugin's window and the plugin**, one
/// way: from the window's thread to the audio thread, or back. Each is the
/// port it is for, the atom's type and its body, of any size up to what the
/// ring holds; a message that does not fit is refused, never waited for.
///
/// One writer and one reader, like rt::SpscQueue, but of bytes, since an
/// atom is as long as it is: an LSP plugin's graph is a few kilobytes, a
/// knob's message a few bytes. Nothing is allocated after it is made.
class AtomRing
{
public:
    struct Header {
        std::uint32_t port = 0;
        std::uint32_t type = 0;
        std::uint32_t size = 0;
    };

    /// Room for \a capacity bytes, made a power of two.
    explicit AtomRing(std::size_t capacity)
        : m_bytes(std::bit_ceil(std::max<std::size_t>(capacity, 64)))
        , m_mask(m_bytes.size() - 1)
    {
    }

    AtomRing(const AtomRing &) = delete;
    AtomRing &operator=(const AtomRing &) = delete;

    /// Adds one message, or returns false when there is no room for it.
    [[nodiscard]] bool
    write(std::uint32_t port, std::uint32_t type, std::uint32_t size, const void *body) noexcept RB_NONBLOCKING
    {
        const std::size_t write = m_write.load(std::memory_order_relaxed);
        const std::size_t read = m_read.load(std::memory_order_acquire);
        const std::size_t total = sizeof(Header) + size;
        if (m_bytes.size() - (write - read) < total) {
            return false;
        }
        const Header header{port, type, size};
        copyIn(write, &header, sizeof(header));
        copyIn(write + sizeof(header), body, size);
        m_write.store(write + total, std::memory_order_release);
        return true;
    }

    /// The next message's header, left where it is; false when none waits.
    [[nodiscard]] bool peek(Header &header) const noexcept RB_NONBLOCKING
    {
        const std::size_t read = m_read.load(std::memory_order_relaxed);
        const std::size_t write = m_write.load(std::memory_order_acquire);
        if (write - read < sizeof(Header)) {
            return false;
        }
        copyOut(read, &header, sizeof(header));
        return true;
    }

    /// Takes the next message into \a header and its body into \a body, if
    /// one is waiting and its body fits in \a room bytes. A message too
    /// large for \a room is let go of, so the ones after it still come.
    [[nodiscard]] bool read(Header &header, void *body, std::size_t room) noexcept RB_NONBLOCKING
    {
        for (;;) {
            const std::size_t read = m_read.load(std::memory_order_relaxed);
            const std::size_t write = m_write.load(std::memory_order_acquire);
            if (write - read < sizeof(Header)) {
                return false;
            }
            copyOut(read, &header, sizeof(header));
            const bool fits = header.size <= room;
            if (fits) {
                copyOut(read + sizeof(header), body, header.size);
            }
            m_read.store(read + sizeof(header) + header.size, std::memory_order_release);
            if (fits) {
                return true;
            }
        }
    }

    /// Lets go of everything waiting: for a window that has gone, whose
    /// messages nobody will read. The reading side only.
    void clear() noexcept
    {
        m_read.store(m_write.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    void copyIn(std::size_t at, const void *from, std::size_t size) noexcept RB_NONBLOCKING
    {
        if (size == 0) {
            return;
        }
        const std::size_t start = at & m_mask;
        const std::size_t first = std::min(size, m_bytes.size() - start);
        std::memcpy(m_bytes.data() + start, from, first);
        std::memcpy(m_bytes.data(), static_cast<const std::uint8_t *>(from) + first, size - first);
    }

    void copyOut(std::size_t at, void *into, std::size_t size) const noexcept RB_NONBLOCKING
    {
        if (size == 0) {
            return;
        }
        const std::size_t start = at & m_mask;
        const std::size_t first = std::min(size, m_bytes.size() - start);
        std::memcpy(into, m_bytes.data() + start, first);
        std::memcpy(static_cast<std::uint8_t *>(into) + first, m_bytes.data(), size - first);
    }

    std::vector<std::uint8_t> m_bytes;
    std::size_t m_mask = 0;
    alignas(rt::kCacheLineBytes) std::atomic<std::size_t> m_write{0};
    alignas(rt::kCacheLineBytes) std::atomic<std::size_t> m_read{0};
};

} // namespace rb::pluginhost
