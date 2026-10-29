// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::dsp
{

/// A fast Fourier transform of a real signal whose length is a power of
/// two, and its inverse, for the audio thread.
///
/// **Ours, not a library's** (ADR-0015). FFTW is GPL and cannot be linked
/// into an MPL program that has to stay combinable with proprietary code;
/// PFFFT and KISS FFT are fine by licence but would be the first vendored C
/// in the tree, and what the blocks need — one real transform of a fixed
/// size a few hundred times a second — is small enough to own: a radix-2
/// complex transform of half the length, and the step that turns it into a
/// real one.
///
/// **Everything is made in prepare()**: the twiddles, the bit-reversal
/// table and the work space. forward() and inverse() then allocate
/// nothing, take no lock and do O(N log N) work, so a block may call them
/// from process().
///
/// The spectrum is the N / 2 + 1 bins from 0 Hz to half the rate. forward()
/// does not scale; inverse() divides by N, so inverse(forward(x)) is x.
class RealFft
{
public:
    /// Makes the tables for a transform of \a size samples, a power of two
    /// of at least 4. **Not for the audio thread.**
    void prepare(std::size_t size);

    [[nodiscard]] std::size_t size() const noexcept RB_NONBLOCKING
    {
        return m_size;
    }

    /// How many bins a spectrum has: size() / 2 + 1.
    [[nodiscard]] std::size_t bins() const noexcept RB_NONBLOCKING
    {
        return m_size / 2 + 1;
    }

    /// The spectrum of \a signal (size() samples) into \a spectrum (bins()
    /// values).
    void forward(std::span<const float> signal, std::span<std::complex<float>> spectrum) noexcept RB_NONBLOCKING;

    /// The signal whose spectrum is \a spectrum (bins() values) into
    /// \a signal (size() samples). The imaginary parts of the first and the
    /// last bin are taken as zero, as a real signal's are.
    void inverse(std::span<const std::complex<float>> spectrum, std::span<float> signal) noexcept RB_NONBLOCKING;

private:
    /// The complex transform of the work space in place, half the size.
    void transform(bool inverse) noexcept RB_NONBLOCKING;

    std::size_t m_size = 0;
    std::size_t m_half = 0;
    /// e^(-2πik/half), for the complex transform.
    std::vector<std::complex<float>> m_twiddles;
    /// e^(-2πik/size), for turning half a complex transform into a real one.
    std::vector<std::complex<float>> m_realTwiddles;
    std::vector<std::uint32_t> m_reversed;
    std::vector<std::complex<float>> m_work;
};

} // namespace rb::dsp
