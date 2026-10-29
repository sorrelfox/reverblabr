// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "allocationtrap.h"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace rb::test
{
namespace
{

/// The innermost guard on this thread, or nothing. Per thread, so that a
/// worker allocating does not trip a guard on the audio thread.
thread_local NoAllocations *t_armed = nullptr;

} // namespace

NoAllocations::NoAllocations(OnAllocation onAllocation) noexcept
    : m_outer(t_armed)
    , m_onAllocation(onAllocation)
{
    t_armed = this;
}

NoAllocations::~NoAllocations() noexcept
{
    t_armed = m_outer;
}

std::size_t NoAllocations::count() const noexcept
{
    return m_count;
}

bool NoAllocations::isArmed() noexcept
{
    return t_armed != nullptr;
}

void noteAllocation(const char *what) noexcept
{
    NoAllocations *const guard = t_armed;
    if (guard == nullptr) {
        return;
    }
    // Disarmed while this one is dealt with, so that anything the reporting
    // itself does cannot come back round.
    t_armed = nullptr;
    guard->m_count += 1;
    const NoAllocations::OnAllocation reaction = guard->m_onAllocation;
    t_armed = guard;

    if (reaction == NoAllocations::OnAllocation::Abort) {
        std::fputs("\nreverblabr: ", stderr);
        std::fputs(what, stderr);
        std::fputs(
            " on a path that must not touch the allocator.\n"
            "The real-time rules are in docs/architecture.md; prepare() is where memory is taken.\n",
            stderr);
        std::fflush(stderr);
        std::abort();
    }
}

} // namespace rb::test

#if RB_TRAP_ALLOCATIONS

// The global operators, replaced for every test binary that links the kit.
// Each one notes what happened and then does what it would have done.

void *operator new(std::size_t size)
{
    rb::test::noteAllocation("memory was allocated");
    void *const memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    rb::test::noteAllocation("memory was allocated");
    return std::malloc(size == 0 ? 1 : size);
}

void *operator new[](std::size_t size, const std::nothrow_t &tag) noexcept
{
    return ::operator new(size, tag);
}

void operator delete(void *memory) noexcept
{
    rb::test::noteAllocation("memory was freed");
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    ::operator delete(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete(void *memory, const std::nothrow_t &) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void *memory, const std::nothrow_t &) noexcept
{
    ::operator delete(memory);
}

#endif // RB_TRAP_ALLOCATIONS
