// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define RB_DENORMALS_X86 1
#elif defined(__aarch64__)
#define RB_DENORMALS_AARCH64 1
#endif

namespace rb::dsp
{

/// Flushes denormal numbers to zero for as long as it is in scope, and puts
/// the previous setting back on the way out.
///
/// A filter or an envelope that decays towards zero eventually produces
/// denormals, numbers so small that the processor handles them in microcode.
/// A cycle that is normally microseconds long can then take hundreds of
/// times as long and miss its deadline, which is heard as a dropout. Treating
/// them as zero costs nothing musically: they are far below the level of the
/// least significant bit of any output format.
///
/// The flag belongs to the thread, not to the code, so the guard goes at the
/// top of the process callback, once per cycle:
///
///     void process(ProcessContext &context) noexcept RB_NONBLOCKING
///     {
///         const ScopedDenormalsOff denormalsOff;
///         ...
///     }
///
/// Restoring the previous setting matters because the callback runs on a
/// thread we don't own: PipeWire, or another filter on the same data loop,
/// may expect its own setting.
///
/// On x86 this sets MXCSR's flush-to-zero and denormals-are-zero bits; on
/// AArch64 it sets FPCR's FZ bit. On any other architecture it does nothing,
/// which is safe, only slower in the rare case.
class ScopedDenormalsOff
{
public:
    ScopedDenormalsOff() noexcept RB_NONBLOCKING
    {
#if defined(RB_DENORMALS_X86)
        m_saved = _mm_getcsr();
        _mm_setcsr(m_saved | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);
#elif defined(RB_DENORMALS_AARCH64)
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(m_saved));
        const std::uint64_t flushToZero = m_saved | (std::uint64_t{1} << 24);
        __asm__ __volatile__("msr fpcr, %0" : : "r"(flushToZero));
#endif
    }

    ~ScopedDenormalsOff() noexcept RB_NONBLOCKING
    {
#if defined(RB_DENORMALS_X86)
        _mm_setcsr(m_saved);
#elif defined(RB_DENORMALS_AARCH64)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(m_saved));
#endif
    }

    ScopedDenormalsOff(const ScopedDenormalsOff &) = delete;
    ScopedDenormalsOff &operator=(const ScopedDenormalsOff &) = delete;
    ScopedDenormalsOff(ScopedDenormalsOff &&) = delete;
    ScopedDenormalsOff &operator=(ScopedDenormalsOff &&) = delete;

    /// Whether this build can turn denormals off at all. Tests use it to skip
    /// the measurement on an architecture where the guard does nothing.
    [[nodiscard]] static constexpr bool isSupported() noexcept
    {
#if defined(RB_DENORMALS_X86) || defined(RB_DENORMALS_AARCH64)
        return true;
#else
        return false;
#endif
    }

    /// Whether denormals are being flushed on this thread right now.
    [[nodiscard]] static bool isFlushingToZero() noexcept RB_NONBLOCKING
    {
#if defined(RB_DENORMALS_X86)
        return (_mm_getcsr() & _MM_FLUSH_ZERO_ON) != 0;
#elif defined(RB_DENORMALS_AARCH64)
        std::uint64_t fpcr = 0;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        return (fpcr & (std::uint64_t{1} << 24)) != 0;
#else
        return false;
#endif
    }

private:
#if defined(RB_DENORMALS_X86)
    unsigned int m_saved = 0;
#elif defined(RB_DENORMALS_AARCH64)
    std::uint64_t m_saved = 0;
#endif
};

} // namespace rb::dsp
