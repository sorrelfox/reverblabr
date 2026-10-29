// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/denormals.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <ios>
#include <limits>

using namespace rb::dsp;

namespace
{

/// The smallest number float can hold without losing precision. Halving it
/// again gives a denormal, unless the processor is flushing them to zero.
constexpr float kSmallestNormal = std::numeric_limits<float>::min();

/// Halves a value while the program is running, which is the only place the
/// processor's flush-to-zero flag has a say.
///
/// `[[gnu::noinline]]` alone did not achieve that, so in an optimised build
/// this measured the compiler rather than the guard: GCC's interprocedural
/// constant propagation cloned the function with the argument baked in and
/// folded the multiplication there, leaving two compile-time constants to
/// compare. The empty `asm volatile` takes the value through a construct the
/// optimiser may not look into, which leaves it nothing to fold, and it
/// means the same thing to GCC and Clang; `[[gnu::noipa]]`, the other cure,
/// is a GCC-only spelling that Clang rejects as an unknown attribute, and
/// warnings are errors here. The attribute stays because the call boundary
/// also keeps the multiplication where it was written, inside the scope of
/// the guard under test.
[[gnu::noinline]] float halve(float value)
{
    __asm__ __volatile__("" : "+m"(value));
    return value * 0.5f;
}

} // namespace

TEST(ScopedDenormalsOff, FlushesDenormalsWhileInScope)
{
    if (!ScopedDenormalsOff::isSupported()) {
        GTEST_SKIP() << "no flush-to-zero control on this architecture";
    }

    const ScopedDenormalsOff denormalsOff;
    EXPECT_TRUE(ScopedDenormalsOff::isFlushingToZero());

    // Compared as bits, because a denormal prints as "0" at gtest's default
    // float precision, and a failure would otherwise read "0 != 0".
    const float flushed = halve(kSmallestNormal);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(flushed), 0U)
        << "a denormal result has to come out as zero, not " << std::hexfloat << flushed;
}

TEST(ScopedDenormalsOff, PutsThePreviousSettingBack)
{
    if (!ScopedDenormalsOff::isSupported()) {
        GTEST_SKIP() << "no flush-to-zero control on this architecture";
    }

    // The flag belongs to the thread, and the audio thread is not ours, so
    // leaving it changed would change how somebody else's code computes.
    const bool before = ScopedDenormalsOff::isFlushingToZero();
    {
        const ScopedDenormalsOff denormalsOff;
        EXPECT_TRUE(ScopedDenormalsOff::isFlushingToZero());
    }
    EXPECT_EQ(ScopedDenormalsOff::isFlushingToZero(), before);
}

TEST(ScopedDenormalsOff, NestsWithoutLosingTheOuterSetting)
{
    if (!ScopedDenormalsOff::isSupported()) {
        GTEST_SKIP() << "no flush-to-zero control on this architecture";
    }

    const ScopedDenormalsOff outer;
    {
        const ScopedDenormalsOff inner;
        EXPECT_TRUE(ScopedDenormalsOff::isFlushingToZero());
    }
    EXPECT_TRUE(ScopedDenormalsOff::isFlushingToZero());
}

TEST(ScopedDenormalsOff, WithoutTheGuardDenormalsSurvive)
{
    if (!ScopedDenormalsOff::isSupported()) {
        GTEST_SKIP() << "no flush-to-zero control on this architecture";
    }
    ASSERT_FALSE(ScopedDenormalsOff::isFlushingToZero()) << "a previous test left the flag set";

    // Proves the test above measures the guard and not the compiler: with the
    // default setting the same computation keeps its denormal result.
    const float denormal = halve(kSmallestNormal);
    EXPECT_GT(denormal, 0.0f);
    EXPECT_LT(denormal, kSmallestNormal);
    EXPECT_FALSE(std::isnormal(denormal));
}
