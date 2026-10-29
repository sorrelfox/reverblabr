// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"
#include "rt/spscqueue.h"

#include <atomic>
#include <cstddef>
#include <memory>

namespace rb::rt
{

/// Where the audio thread puts objects it has finished with, so that another
/// thread frees them.
///
/// Freeing memory takes a lock inside the allocator, which is exactly what
/// the audio thread must not do, so it never calls delete. When it drops a
/// compiled graph, a buffer or a block it hands the pointer over here, and
/// the engine thread empties the queue whenever it next runs.
///
/// The handover copies a pointer and a function that knows how to delete it,
/// so nothing is allocated and the type is not lost.
///
/// If the queue is full, tryRetire() refuses and **leaves the caller owning
/// the object**, because dropping it there would mean freeing it on the audio
/// thread. The caller keeps it and offers it again next cycle. Size the queue
/// so this does not happen; retireRefusedCount() says whether it did.
class RetireQueue
{
public:
    /// Builds a queue that can hold \a capacity objects waiting to be freed.
    explicit RetireQueue(std::size_t capacity)
        : m_queue(capacity)
    {
    }

    /// Frees anything still waiting. Runs wherever the queue is destroyed,
    /// which is not the audio thread.
    ~RetireQueue()
    {
        drain();
    }

    RetireQueue(const RetireQueue &) = delete;
    RetireQueue &operator=(const RetireQueue &) = delete;

    /// Hands \a object over to be freed elsewhere, and returns whether it was
    /// taken. On success \a object is empty; on failure it still owns what it
    /// did, and nothing has been freed. Safe on the audio thread.
    template <typename T>
    [[nodiscard]] bool tryRetire(std::unique_ptr<T> &object) noexcept RB_NONBLOCKING
    {
        if (object == nullptr) {
            return true;
        }
        const Entry entry{object.get(), &destroyAs<T>};
        if (!m_queue.tryPush(entry)) {
            m_refused.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        static_cast<void>(object.release());
        return true;
    }

    /// The same for an object the audio thread holds by a plain pointer,
    /// which is null once it has been taken. **For code checked as
    /// nonblocking**: a std::unique_ptr held there has a destructor and an
    /// assignment that may free, and the checker refuses both even where the
    /// pointer is always empty by then.
    template <typename T>
    [[nodiscard]] bool tryRetire(T *&object) noexcept RB_NONBLOCKING
    {
        if (object == nullptr) {
            return true;
        }
        const Entry entry{object, &destroyAs<T>};
        if (!m_queue.tryPush(entry)) {
            m_refused.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        object = nullptr;
        return true;
    }

    /// Frees everything waiting and returns how many objects that was. Call
    /// this from a normal thread only; it is where the destructors run.
    std::size_t drain()
    {
        std::size_t freed = 0;
        Entry entry{};
        while (m_queue.tryPop(entry)) {
            entry.destroy(entry.object);
            ++freed;
        }
        return freed;
    }

    /// How many objects are waiting to be freed.
    [[nodiscard]] std::size_t pending() const noexcept RB_NONBLOCKING
    {
        return m_queue.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept RB_NONBLOCKING
    {
        return m_queue.capacity();
    }

    /// How many times tryRetire() has had to refuse because the queue was
    /// full. Anything but zero means the queue is too small for the rate of
    /// change, and belongs in the diagnostics.
    [[nodiscard]] std::size_t retireRefusedCount() const noexcept RB_NONBLOCKING
    {
        return m_refused.load(std::memory_order_relaxed);
    }

private:
    /// A pointer and the one function that knows its real type. It carries
    /// no default member initialisers on purpose: SpscQueue<Entry> is built
    /// as a member below, while RetireQueue is still an incomplete type, and
    /// an initialiser could not be used that early.
    struct Entry {
        void *object;
        void (*destroy)(void *);
    };

    template <typename T>
    static void destroyAs(void *object)
    {
        delete static_cast<T *>(object);
    }

    SpscQueue<Entry> m_queue;
    std::atomic<std::size_t> m_refused{0};
};

} // namespace rb::rt
