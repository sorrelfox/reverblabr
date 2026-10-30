// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/fft.h"
#include "rt/realtime.h"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::dsp
{

/// How a move carries the sound of its source range into its destination.
enum class MoveMethod : std::uint8_t {
    /// Squeezes the whole source range into the destination, spaced evenly
    /// in octaves: the source's bottom lands on the destination's bottom and
    /// its top on the destination's top. For a range the ear has no use for
    /// at all, which is usually emptied as well (MoveSource::Remove).
    Compress = 0,
    /// Copies the source by one ratio, the destination's bottom over the
    /// source's (about two thirds of an octave down with Frequency
    /// Lowering's default ranges, 4 kHz to 2.5 kHz; two octaves up with
    /// Frequency Raising's, 250 Hz to 1 kHz), so the sound's harmonics
    /// keep their spacing; whatever lands above the destination's top is
    /// left out. For a range the ear still hears a little of, which is
    /// usually kept (MoveSource::Keep).
    Transpose = 1,
    /// Frequency Raising only, and experimental: a sound in the source
    /// is not moved but given its harmonics — twice its frequency, three
    /// times and so on, up to kMaxHarmonic — wherever they fall inside the
    /// destination. The ear hears a fundamental's pitch from its harmonics,
    /// so a voice keeps its pitch where moving it up would raise it. Each
    /// harmonic has the level of the sound it comes from, and the move's.
    Harmonics = 2,
};

/// What becomes of the source range once its sound has been moved.
enum class MoveSource : std::uint8_t {
    Keep = 0,
    /// kLowerDb quieter.
    Lower = 1,
    Remove = 2,
};

/// When a move acts.
enum class MoveWhen : std::uint8_t {
    Always = 0,
    /// Only while the source range holds most of the sound: the hiss of an
    /// s, sh or f, and not a vowel or a chord, which pass as they came.
    WhenNeeded = 1,
};

/// One move: sound from a source range carried into a destination range:
/// below it for Frequency Lowering, above it for Frequency Raising.
/// raises() says which.
struct FrequencyMove {
    /// Whether the move acts at all: used and switched on.
    bool active = false;
    MoveMethod method = MoveMethod::Compress;
    float sourceLowHz = 4000.0f;
    float sourceHighHz = 8000.0f;
    float destinationLowHz = 2500.0f;
    float destinationHighHz = 4000.0f;
    /// The moved sound's level against the source's, in dB.
    float levelDb = 0.0f;
    MoveSource source = MoveSource::Remove;
    MoveWhen when = MoveWhen::Always;

    /// The same carrying: the same ranges and the same method. A move whose
    /// carrying changes starts its peaks' phases afresh.
    [[nodiscard]] bool carriesLike(const FrequencyMove &other) const noexcept RB_NONBLOCKING;
    /// Whether the destination reaches above the source: a raising.
    [[nodiscard]] bool raises() const noexcept RB_NONBLOCKING
    {
        return destinationHighHz > sourceHighHz;
    }
};

/// The highest harmonic MoveMethod::Harmonics gives a sound: its sixteenth.
inline constexpr int kMaxHarmonic = 16;

/// Where \a move carries a sound at \a hz, or 0 when it carries nothing
/// from there: \a hz is outside the source range, or a transposition puts
/// it above the destination's top. For MoveMethod::Harmonics, the lowest of
/// its harmonics inside the destination, or 0 if none is. Works for any
/// rate; the mover asks it for every peak it moves.
[[nodiscard]] double carriedTo(const FrequencyMove &move, double hz) noexcept RB_NONBLOCKING;

/// Moves sound out of up to kMaxMoves source ranges into destination ranges,
/// on one channel: the engine of Frequency Lowering, which carries
/// sound down, and of Frequency Raising, which carries it up.
///
/// **A short-time Fourier transform** on RealFft (ADR-0015): frames of at
/// least the length prepare() is given (a power of two: 1024 samples at
/// 48 kHz for kFrameMs, 2048 for kMiddleFrameMs, 4096 for kLowFrameMs), a
/// Hann window going in
/// and coming out, a quarter of a frame apart. Those windows add up to the
/// same everywhere, so with nothing to move the channel comes out exactly as
/// it went in, one frame later: latencySamples().
///
/// **Peaks moved, not bins** (Laroche and Dolson, 1999). Each frame's
/// spectrum is cut into regions round its peaks, halfway to the next peak
/// each side, and a peak inside a move's source range is carried where
/// carriedTo() says with the bins round it, whole, so a tone keeps its shape
/// however bent the map is. Its phase goes on from where the same peak's
/// was a frame ago, at the frequency it is carried to, so the moved sound
/// is one steady tone rather than a new one every frame; the bins round it
/// keep their phases relative to it. Noise is peaks too, close together,
/// and every bin of a source range belongs to one of them, so all of the
/// range's sound goes.
///
/// **Every move reads the input**, never another move's output: their order
/// does not matter and nothing is moved twice. Where destinations overlap,
/// what lands there adds up.
///
/// **Nothing steps, with nothing to glide.** A change reaches the next
/// frame whole — a move on or off, a level, its ranges, "when needed"
/// deciding it is needed — and every frame comes out through a window that
/// overlaps three others, so a change fades in over a frame (about 21 ms at
/// 48 kHz) by itself. Glides and a crossfade on top of that were measured
/// and changed nothing: not the largest step, and not a trace of a click
/// above 10 kHz.
///
/// Allocation-free after prepare().
class FrequencyMover
{
public:
    static constexpr std::size_t kMaxMoves = 4;
    /// Frames for moving high sound: fine in time, about 47 Hz a bin at
    /// 48 kHz, plenty where a peak is thousands of hertz up.
    static constexpr double kFrameMs = 20.0;
    /// Frames for moving middle sound: about 23 Hz a bin at 48 kHz (2048
    /// samples, about 43 ms of latency), enough to tell apart the
    /// harmonics of a voice from 250 Hz up.
    static constexpr double kMiddleFrameMs = 40.0;
    /// Frames for moving low sound: about 12 Hz a bin at 48 kHz, so
    /// the harmonics of a 100 Hz voice are peaks of their own, at the cost
    /// of about 85 ms of latency.
    static constexpr double kLowFrameMs = 80.0;
    /// How much quieter the source range becomes with MoveSource::Lower.
    static constexpr float kLowerDb = -12.0f;
    /// "When needed": a move acts while its source range is louder, by this
    /// ratio in power, than everything on its destination's side of it —
    /// below it from kWhenNeededFromHz up for a lowering, above it up to
    /// kWhenNeededToHz for a raising. An s or an f puts most of its power
    /// high and little under it; a vowel or a hum puts most of it low and
    /// little over it. Against the whole spectrum instead, a hiss reaching
    /// above the source range, as a real s does, would never count.
    static constexpr double kWhenNeededRatio = 1.0;
    static constexpr double kWhenNeededFromHz = 100.0;
    static constexpr double kWhenNeededToHz = 16000.0;
    /// A bin quieter than this, over the frame's size, is silence: no peak
    /// is found in it, and a source range of it never needs moving. About
    /// -140 dB against a full-scale sine.
    static constexpr double kSilentMagnitude = 1.0e-7;

    /// Makes the transform, the windows and the work space for
    /// \a sampleRate, with frames of at least \a frameMs. **Not for the
    /// audio thread.**
    void prepare(double sampleRate, double frameMs = kFrameMs);

    /// Samples per frame at the rate it was prepared for.
    [[nodiscard]] std::size_t frameSize() const noexcept RB_NONBLOCKING
    {
        return m_size;
    }
    /// How far apart two frames are.
    [[nodiscard]] std::size_t hop() const noexcept RB_NONBLOCKING
    {
        return m_hop;
    }
    /// How much later the channel comes out: one frame.
    [[nodiscard]] std::uint32_t latencySamples() const noexcept RB_NONBLOCKING
    {
        return static_cast<std::uint32_t>(m_size);
    }

    /// Sets move \a slot (below kMaxMoves). Takes effect at the next frame.
    void setMove(std::size_t slot, const FrequencyMove &move) noexcept RB_NONBLOCKING;
    [[nodiscard]] const FrequencyMove &move(std::size_t slot) const noexcept RB_NONBLOCKING;

    /// Whether the channel carries only what the moves add, without the
    /// sound that was already there: for setting a move's level by ear.
    void setMovedOnly(bool movedOnly) noexcept RB_NONBLOCKING;

    /// Whether move \a slot acted on the last frame: it is on, and for "when
    /// needed", its source range was louder than what is below it.
    [[nodiscard]] bool acting(std::size_t slot) const noexcept RB_NONBLOCKING;

    /// Forgets the sound so far; the moves stay as they are.
    void reset() noexcept RB_NONBLOCKING;

    /// Moves \a input into \a output (the same length; they may be the same
    /// samples).
    void process(std::span<const float> input, std::span<float> output) noexcept RB_NONBLOCKING;

private:
    /// A move as it is carried out.
    struct Slot {
        FrequencyMove move;
        /// Whether it acted on the last frame.
        bool acting = false;
        /// Each peak's phase as it was carried out last frame, by the bin
        /// the peak was in, and whether that bin held a peak; this frame's
        /// go in the other of each pair.
        std::array<std::vector<float>, 2> peakPhase;
        std::array<std::vector<std::uint8_t>, 2> wasPeak;
        std::size_t current = 0;
    };

    /// Works out one frame: the frame's spectrum, the moves, and the frame
    /// back into the output.
    void frame() noexcept RB_NONBLOCKING;
    /// Carries the peaks of \a slot's source range into m_moved.
    void carry(Slot &slot) noexcept RB_NONBLOCKING;
    /// Gives each peak of \a slot's source range its harmonics inside the
    /// destination, in m_moved (MoveMethod::Harmonics).
    void harmonics(const Slot &slot) noexcept RB_NONBLOCKING;
    /// Adds the bins \a from to \a to round \a peak into m_moved, \a shift
    /// bins along, at \a gain, the peak's phase \a phase and the rest's
    /// kept against it.
    void place(std::size_t peak, std::size_t from, std::size_t to, long shift, float gain, double phase) noexcept
        RB_NONBLOCKING;
    /// The bins round the \a index-th peak that go with it: halfway to the
    /// next peak either side, within \a low to \a high.
    void regionOf(std::size_t index,
                  std::size_t low,
                  std::size_t high,
                  std::size_t &from,
                  std::size_t &to) const noexcept RB_NONBLOCKING;
    /// The true frequency of the peak at \a peak, from how far its phase
    /// went since the last frame.
    [[nodiscard]] double trueHz(std::size_t peak) const noexcept RB_NONBLOCKING;
    /// Whether \a slot's move acts on this frame.
    [[nodiscard]] bool needed(const Slot &slot) const noexcept RB_NONBLOCKING;
    [[nodiscard]] std::size_t binOf(double hz) const noexcept RB_NONBLOCKING;

    double m_sampleRate = 0.0;
    std::size_t m_size = 0;
    std::size_t m_hop = 0;
    std::size_t m_bins = 0;
    RealFft m_fft;
    std::vector<float> m_window;
    /// The window again, scaled so that the windows going in and coming out
    /// add up to one across overlapping frames.
    std::vector<float> m_synthesis;
    std::vector<float> m_frame;
    std::vector<float> m_work;
    std::vector<float> m_sum;
    std::vector<float> m_out;
    std::size_t m_position = 0;
    std::vector<std::complex<float>> m_spectrum;
    std::vector<std::complex<float>> m_moved;
    std::vector<float> m_magnitude;
    std::vector<float> m_phase;
    std::vector<float> m_lastPhase;
    std::vector<float> m_keep;
    /// This frame's peaks, lowest first: the first m_peakCount of them.
    std::vector<std::uint32_t> m_peaks;
    std::size_t m_peakCount = 0;
    std::array<Slot, kMaxMoves> m_slots;
    bool m_movedOnly = false;
};

} // namespace rb::dsp
