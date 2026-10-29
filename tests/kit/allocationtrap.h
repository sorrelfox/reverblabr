// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>

// Whether this build may replace the global operator new and operator delete.
// AddressSanitizer and ThreadSanitizer own the allocator themselves, and
// replacing it underneath them is asking for trouble; under Clang, where
// those two live, RealtimeSanitizer covers the same ground anyway.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define RB_TRAP_ALLOCATIONS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define RB_TRAP_ALLOCATIONS 0
#endif
#endif
#ifndef RB_TRAP_ALLOCATIONS
#define RB_TRAP_ALLOCATIONS 1
#endif

namespace rb::test
{

/// Catches memory being taken or given back while a stretch of code runs.
///
/// This is the real-time check that works with GCC. Clang has two of its own
/// — `-Wfunction-effects` at compile time and RealtimeSanitizer at run time,
/// both driven by `RB_NONBLOCKING` — but GCC has neither, so the DSP test kit
/// replaces the global `operator new` and `operator delete` and watches them
/// while the guard is in scope.
///
/// Freeing counts as well as allocating: both take the allocator's lock, and
/// the audio thread may do neither.
///
///     {
///         const rb::test::NoAllocations realTime;
///         block.process(context);
///     } // aborts the test at the first allocation inside process()
///
/// The guard is **per thread**, so one thread arming it does not trip
/// another. Guards nest; the innermost decides what happens.
class NoAllocations
{
public:
    enum class OnAllocation {
        /// Print where it happened and stop the test binary. This is what a
        /// real-time path deserves: the mistake is not a smaller number, it
        /// is the allocation.
        Abort,
        /// Count them and carry on, so that a test can say how many there
        /// were. Used to prove the trap itself works.
        Count,
    };

    explicit NoAllocations(OnAllocation onAllocation = OnAllocation::Abort) noexcept;
    ~NoAllocations() noexcept;

    NoAllocations(const NoAllocations &) = delete;
    NoAllocations &operator=(const NoAllocations &) = delete;
    NoAllocations(NoAllocations &&) = delete;
    NoAllocations &operator=(NoAllocations &&) = delete;

    /// How many times memory was taken or given back since this guard was
    /// made.
    [[nodiscard]] std::size_t count() const noexcept;

    /// Whether any guard is watching this thread right now.
    [[nodiscard]] static bool isArmed() noexcept;

    /// Whether this build can see allocations at all. False under Address-
    /// and ThreadSanitizer, where the allocator is already somebody else's.
    /// A test that is about the trap itself skips when this is false.
    [[nodiscard]] static constexpr bool isSupported() noexcept
    {
        return RB_TRAP_ALLOCATIONS != 0;
    }

private:
    friend void noteAllocation(const char *what) noexcept;

    NoAllocations *m_outer = nullptr;
    OnAllocation m_onAllocation = OnAllocation::Abort;
    std::size_t m_count = 0;
};

} // namespace rb::test
