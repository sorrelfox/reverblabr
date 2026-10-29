// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/frequencymove.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::dsp
{

namespace
{

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// \a phase brought back into (-π, π], where a float still has its
/// precision.
[[nodiscard]] double wrapped(double phase) noexcept RB_NONBLOCKING
{
    return phase - kTwoPi * std::round(phase / kTwoPi);
}

/// What becomes of the source range's sound, as a gain.
[[nodiscard]] float sourceGainOf(MoveSource source) noexcept RB_NONBLOCKING
{
    switch (source) {
    case MoveSource::Keep:
        return 1.0f;
    case MoveSource::Lower:
        return dbToGain(FrequencyMover::kLowerDb);
    case MoveSource::Remove:
        return 0.0f;
    }
    return 1.0f;
}

/// What a frame of \a size samples through the periodic Hann window makes of
/// a complex tone \a x bins from bin k, as forward() gives it: the spectrum
/// at k of w[n] e^(2πi(k - x)n/size). The window is 0.5 - 0.25 e^(2πin/N)
/// - 0.25 e^(-2πin/N), so this is three Dirichlet kernels a bin apart.
[[nodiscard]] std::complex<double> hannKernel(double x, double size) noexcept RB_NONBLOCKING
{
    const auto dirichlet = [size](double at) noexcept RB_NONBLOCKING {
        const double below = std::sin(std::numbers::pi * at / size);
        // Real, and negative past the main lobe: not a magnitude.
        const double real = std::abs(below) < 1.0e-12 ? size : std::sin(std::numbers::pi * at) / below;
        return real * std::polar(1.0, -std::numbers::pi * at * (size - 1.0) / size);
    };
    return 0.5 * dirichlet(x) - 0.25 * dirichlet(x - 1.0) - 0.25 * dirichlet(x + 1.0);
}

} // namespace

bool FrequencyMove::carriesLike(const FrequencyMove &other) const noexcept RB_NONBLOCKING
{
    return method == other.method && sourceLowHz == other.sourceLowHz && sourceHighHz == other.sourceHighHz
        && destinationLowHz == other.destinationLowHz && destinationHighHz == other.destinationHighHz;
}

double carriedTo(const FrequencyMove &move, double hz) noexcept RB_NONBLOCKING
{
    const double sourceLow = move.sourceLowHz;
    const double sourceHigh = move.sourceHighHz;
    const double destinationLow = move.destinationLowHz;
    const double destinationHigh = move.destinationHighHz;
    if (!(hz >= sourceLow && hz <= sourceHigh) || sourceLow <= 0.0 || destinationLow <= 0.0) {
        return 0.0;
    }
    if (move.method == MoveMethod::Harmonics) {
        for (int harmonic = 2; harmonic <= kMaxHarmonic; ++harmonic) {
            const double to = hz * harmonic;
            if (to > destinationHigh) {
                break;
            }
            if (to >= destinationLow) {
                return to;
            }
        }
        return 0.0;
    }
    if (move.method == MoveMethod::Transpose) {
        const double to = hz * destinationLow / sourceLow;
        return to <= destinationHigh ? to : 0.0;
    }
    // Evenly in octaves: as far up the destination, as a share of its
    // octaves, as the sound is up the source.
    if (sourceHigh <= sourceLow || destinationHigh <= destinationLow) {
        return destinationLow;
    }
    const double along = std::log(hz / sourceLow) / std::log(sourceHigh / sourceLow);
    return destinationLow * std::pow(destinationHigh / destinationLow, along);
}

void FrequencyMover::prepare(double sampleRate, double frameMs)
{
    m_sampleRate = sampleRate;
    m_size = 256;
    while (static_cast<double>(m_size) < frameMs * 1.0e-3 * sampleRate) {
        m_size *= 2;
    }
    m_hop = m_size / 4;
    m_bins = m_size / 2 + 1;
    m_fft.prepare(m_size);

    // A periodic Hann window, going in and coming out. At a quarter of a
    // frame apart the squares of four of them add up to 1.5 everywhere, so
    // the one coming out is scaled by that.
    m_window.resize(m_size);
    m_synthesis.resize(m_size);
    for (std::size_t n = 0; n < m_size; ++n) {
        const double hann = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(n) / static_cast<double>(m_size));
        m_window[n] = static_cast<float>(hann);
        m_synthesis[n] = static_cast<float>(hann / 1.5);
    }
    m_frame.assign(m_size, 0.0f);
    m_work.assign(m_size, 0.0f);
    m_sum.assign(m_size, 0.0f);
    m_out.assign(m_hop, 0.0f);
    m_spectrum.assign(m_bins, {});
    m_moved.assign(m_bins, {});
    m_magnitude.assign(m_bins, 0.0f);
    m_phase.assign(m_bins, 0.0f);
    m_lastPhase.assign(m_bins, 0.0f);
    m_keep.assign(m_bins, 1.0f);
    m_peaks.assign(m_bins, 0);
    for (Slot &slot : m_slots) {
        for (std::size_t side = 0; side < 2; ++side) {
            slot.peakPhase[side].assign(m_bins, 0.0f);
            slot.wasPeak[side].assign(m_bins, 0);
        }
    }
    reset();
}

void FrequencyMover::setMove(std::size_t slot, const FrequencyMove &move) noexcept RB_NONBLOCKING
{
    if (slot >= kMaxMoves) {
        return;
    }
    Slot &target = m_slots[slot];
    if (!target.move.carriesLike(move)) {
        std::fill(target.wasPeak[target.current].begin(), target.wasPeak[target.current].end(), 0);
    }
    target.move = move;
}

const FrequencyMove &FrequencyMover::move(std::size_t slot) const noexcept RB_NONBLOCKING
{
    return m_slots[std::min(slot, kMaxMoves - 1)].move;
}

void FrequencyMover::setMovedOnly(bool movedOnly) noexcept RB_NONBLOCKING
{
    m_movedOnly = movedOnly;
}

bool FrequencyMover::acting(std::size_t slot) const noexcept RB_NONBLOCKING
{
    return slot < kMaxMoves && m_slots[slot].acting;
}

void FrequencyMover::reset() noexcept RB_NONBLOCKING
{
    std::fill(m_frame.begin(), m_frame.end(), 0.0f);
    std::fill(m_sum.begin(), m_sum.end(), 0.0f);
    std::fill(m_out.begin(), m_out.end(), 0.0f);
    std::fill(m_lastPhase.begin(), m_lastPhase.end(), 0.0f);
    m_position = 0;
    for (Slot &slot : m_slots) {
        slot.acting = false;
        for (std::size_t side = 0; side < 2; ++side) {
            std::fill(slot.wasPeak[side].begin(), slot.wasPeak[side].end(), 0);
        }
    }
}

std::size_t FrequencyMover::binOf(double hz) const noexcept RB_NONBLOCKING
{
    const double bin = hz * static_cast<double>(m_size) / m_sampleRate;
    return static_cast<std::size_t>(std::clamp(std::lround(bin), 0L, static_cast<long>(m_bins) - 1));
}

bool FrequencyMover::needed(const Slot &slot) const noexcept RB_NONBLOCKING
{
    if (!slot.move.active) {
        return false;
    }
    if (slot.move.when == MoveWhen::Always) {
        return true;
    }
    const std::size_t low = binOf(slot.move.sourceLowHz);
    const std::size_t high = binOf(slot.move.sourceHighHz);
    const auto power = [this](std::size_t from, std::size_t to) noexcept RB_NONBLOCKING {
        double sum = 0.0;
        for (std::size_t bin = from; bin < to; ++bin) {
            sum += static_cast<double>(m_magnitude[bin]) * m_magnitude[bin];
        }
        return sum;
    };
    // Against the side the sound is carried to: what is under a high range
    // being lowered, what is over a low range being raised.
    const double other =
        slot.move.raises() ? power(high + 1, binOf(kWhenNeededToHz) + 1) : power(binOf(kWhenNeededFromHz), low);
    const double source = power(low, high + 1);
    const double floor = kSilentMagnitude * static_cast<double>(m_size);
    return source > floor * floor && source > kWhenNeededRatio * other;
}

void FrequencyMover::regionOf(std::size_t index,
                              std::size_t low,
                              std::size_t high,
                              std::size_t &from,
                              std::size_t &to) const noexcept RB_NONBLOCKING
{
    const std::size_t peak = m_peaks[index];
    from = std::max(low, index > 0 ? (m_peaks[index - 1] + peak) / 2 + 1 : low);
    to = std::min(high, index + 1 < m_peakCount ? (peak + m_peaks[index + 1]) / 2 : high);
}

double FrequencyMover::trueHz(std::size_t peak) const noexcept RB_NONBLOCKING
{
    const double hopRadians = kTwoPi * static_cast<double>(m_hop) / static_cast<double>(m_size);
    const double drift =
        wrapped(static_cast<double>(m_phase[peak]) - m_lastPhase[peak] - hopRadians * static_cast<double>(peak));
    return (static_cast<double>(peak) + drift / hopRadians) * m_sampleRate / static_cast<double>(m_size);
}

void FrequencyMover::place(std::size_t peak,
                           std::size_t from,
                           std::size_t to,
                           long shift,
                           float gain,
                           double phase) noexcept RB_NONBLOCKING
{
    for (std::size_t bin = from; bin <= to; ++bin) {
        const long landing = static_cast<long>(bin) + shift;
        if (landing < 1 || landing >= static_cast<long>(m_bins) - 1) {
            continue;
        }
        const double around = phase + m_phase[bin] - m_phase[peak];
        m_moved[static_cast<std::size_t>(landing)] += std::polar(gain * m_magnitude[bin], static_cast<float>(around));
    }
}

void FrequencyMover::harmonics(const Slot &slot) noexcept RB_NONBLOCKING
{
    const FrequencyMove &move = slot.move;
    const double binHz = m_sampleRate / static_cast<double>(m_size);
    const auto size = static_cast<double>(m_size);
    const std::size_t low = binOf(move.sourceLowHz);
    const std::size_t high = binOf(move.sourceHighHz);
    const double gain = dbToGain(move.levelDb);
    for (std::size_t i = 0; i < m_peakCount; ++i) {
        const std::size_t peak = m_peaks[i];
        if (peak < low || peak > high) {
            continue;
        }
        const double hz = trueHz(peak);
        if (!(hz >= move.sourceLowHz && hz <= move.sourceHighHz)) {
            continue;
        }
        // **The peak as a tone**: its amplitude and its phase at the frame's
        // start, from its bin and what the window makes of a tone where it
        // truly is.
        const std::complex<double> window = hannKernel(static_cast<double>(peak) - hz / binHz, size);
        if (std::abs(window) < 1.0e-9) {
            continue;
        }
        const double halfAmplitude = m_magnitude[peak] / std::abs(window);
        const double phase = static_cast<double>(m_phase[peak]) - std::arg(window);

        for (int harmonic = 2; harmonic <= kMaxHarmonic; ++harmonic) {
            const double at = hz * harmonic;
            if (at > move.destinationHighHz) {
                break;
            }
            if (at < move.destinationLowHz) {
                continue;
            }
            // **Made, not moved**: the harmonic's own windowed tone, where
            // it truly is between bins, so that frames overlapping add up
            // to one steady tone. Moving the peak's bins instead lands them
            // up to half a bin off, and the frames' overlap turns that into
            // sidebands a hop's rate apart — 75 dB under a 100 Hz tone's
            // harmonics, below the destination. Its phase is the peak's
            // times the harmonic: as the peak's goes round once a period,
            // the harmonic's goes round that many times, a steady tone at
            // `at` from one frame to the next with nothing to remember.
            const double centre = at / binHz;
            const std::complex<double> tone = std::polar(gain * halfAmplitude, wrapped(harmonic * phase));
            const auto first = static_cast<long>(std::floor(centre)) - 3;
            const auto last = static_cast<long>(std::ceil(centre)) + 3;
            for (long bin = std::max(1L, first); bin <= std::min(last, static_cast<long>(m_bins) - 2); ++bin) {
                m_moved[static_cast<std::size_t>(bin)] +=
                    std::complex<float>(tone * hannKernel(static_cast<double>(bin) - centre, size));
            }
        }
    }
}

void FrequencyMover::carry(Slot &slot) noexcept RB_NONBLOCKING
{
    const FrequencyMove &move = slot.move;
    const double binHz = m_sampleRate / static_cast<double>(m_size);
    const std::size_t low = binOf(move.sourceLowHz);
    const std::size_t high = binOf(move.sourceHighHz);
    const std::size_t last = slot.current;
    const std::size_t next = 1 - last;
    std::vector<float> &phaseNow = slot.peakPhase[next];
    std::vector<std::uint8_t> &peakNow = slot.wasPeak[next];
    const std::vector<float> &phaseBefore = slot.peakPhase[last];
    const std::vector<std::uint8_t> &peakBefore = slot.wasPeak[last];
    std::fill(
        peakNow.begin() + static_cast<std::ptrdiff_t>(low), peakNow.begin() + static_cast<std::ptrdiff_t>(high) + 1, 0);
    const float gain = dbToGain(move.levelDb);

    for (std::size_t i = 0; i < m_peakCount; ++i) {
        const std::size_t peak = m_peaks[i];
        if (peak < low || peak > high) {
            continue;
        }
        // The bins that go with it: halfway to the next peak either side,
        // within the source range.
        std::size_t from = 0;
        std::size_t to = 0;
        regionOf(i, low, high, from, to);

        // Its true frequency, from how far its phase went since the last
        // frame against how far a bin's would.
        const double hz = trueHz(peak);
        const double carried = carriedTo(move, hz);
        if (carried <= 0.0) {
            continue;
        }
        // **By the distance it is carried, rounded once**: from where the
        // peak truly is, not from its bin, or the lobe lands up to a whole
        // bin off the frequency its phase goes on at, and overlapping
        // frames cancel part of it — 2.2 dB of a 300 Hz tone copied to
        // 1.2 kHz in 4096-sample frames.
        const long shift = std::lround((carried - hz) / binHz);

        // Its phase goes on from the same peak's last frame, at the
        // frequency it is carried to; a peak that was not there starts at
        // its own.
        double phase = m_phase[peak];
        for (const long near : {0L, -1L, 1L}) {
            const long bin = static_cast<long>(peak) + near;
            if (bin >= 0 && bin < static_cast<long>(m_bins) && peakBefore[static_cast<std::size_t>(bin)] != 0) {
                phase = wrapped(phaseBefore[static_cast<std::size_t>(bin)]
                                + kTwoPi * carried * static_cast<double>(m_hop) / m_sampleRate);
                break;
            }
        }
        phaseNow[peak] = static_cast<float>(phase);
        peakNow[peak] = 1;
        place(peak, from, to, shift, gain, phase);
    }
    slot.current = next;
}

void FrequencyMover::frame() noexcept RB_NONBLOCKING
{
    for (std::size_t n = 0; n < m_size; ++n) {
        m_work[n] = m_frame[n] * m_window[n];
    }
    m_fft.forward(m_work, m_spectrum);
    for (std::size_t bin = 0; bin < m_bins; ++bin) {
        m_magnitude[bin] = std::abs(m_spectrum[bin]);
        m_phase[bin] = std::arg(m_spectrum[bin]);
        m_moved[bin] = {};
        m_keep[bin] = 1.0f;
    }

    // Peaks: louder than two bins either side, and above what silence
    // leaves.
    const auto floor = static_cast<float>(kSilentMagnitude * static_cast<double>(m_size));
    m_peakCount = 0;
    for (std::size_t bin = 2; bin + 2 < m_bins; ++bin) {
        const float magnitude = m_magnitude[bin];
        if (magnitude > floor && magnitude > m_magnitude[bin - 1] && magnitude >= m_magnitude[bin + 1]
            && magnitude > m_magnitude[bin - 2] && magnitude >= m_magnitude[bin + 2]) {
            m_peaks[m_peakCount++] = static_cast<std::uint32_t>(bin);
        }
    }

    for (Slot &slot : m_slots) {
        slot.acting = needed(slot);
        if (!slot.acting) {
            std::fill(slot.wasPeak[slot.current].begin(), slot.wasPeak[slot.current].end(), 0);
            continue;
        }
        if (slot.move.method == MoveMethod::Harmonics) {
            harmonics(slot);
        } else {
            carry(slot);
        }
        const float keep = sourceGainOf(slot.move.source);
        for (std::size_t bin = binOf(slot.move.sourceLowHz); bin <= binOf(slot.move.sourceHighHz); ++bin) {
            m_keep[bin] *= keep;
        }
    }

    for (std::size_t bin = 0; bin < m_bins; ++bin) {
        m_spectrum[bin] = m_movedOnly ? m_moved[bin] : m_spectrum[bin] * m_keep[bin] + m_moved[bin];
    }
    std::copy(m_phase.begin(), m_phase.end(), m_lastPhase.begin());
    m_fft.inverse(m_spectrum, m_work);

    for (std::size_t n = 0; n < m_size; ++n) {
        m_sum[n] += m_work[n] * m_synthesis[n];
    }
    std::copy(m_sum.begin(), m_sum.begin() + static_cast<std::ptrdiff_t>(m_hop), m_out.begin());
    std::copy(m_sum.begin() + static_cast<std::ptrdiff_t>(m_hop), m_sum.end(), m_sum.begin());
    std::fill(m_sum.end() - static_cast<std::ptrdiff_t>(m_hop), m_sum.end(), 0.0f);
    std::copy(m_frame.begin() + static_cast<std::ptrdiff_t>(m_hop), m_frame.end(), m_frame.begin());
}

void FrequencyMover::process(std::span<const float> input, std::span<float> output) noexcept RB_NONBLOCKING
{
    const std::size_t newest = m_size - m_hop;
    const std::size_t frames = std::min(input.size(), output.size());
    for (std::size_t n = 0; n < frames; ++n) {
        const float sample = input[n];
        output[n] = m_out[m_position];
        m_frame[newest + m_position] = sample;
        if (++m_position == m_hop) {
            frame();
            m_position = 0;
        }
    }
}

} // namespace rb::dsp
