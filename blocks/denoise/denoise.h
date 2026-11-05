// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/fft.h"
#include "dsp/paramstore.h"
#include "rt/triplebuffer.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A noise the Denoise block has learned, its fingerprint (ADR-0033): for
/// every band of a 2048-point spectrum, how loud the noise gets, and for
/// every octave, how far it ever strays past that at once. Levels are in dB
/// against the band of a full-scale sine.
struct DenoiseProfile {
    static constexpr std::size_t kBins = 1025;
    static constexpr std::size_t kOctaves = 8;

    /// The level each band stays at or under in 99 % of the noise's
    /// frames, frame by frame: what its clicks and ticks reach.
    std::array<float, kBins> now{};
    /// The same for the band's level held, the median of the last
    /// Denoise::kHeldSeconds: what the steady part of the noise reaches.
    std::array<float, kBins> held{};
    /// What an octave (Denoise::octaveOf) has to add up to, now and held,
    /// to hold more than the noise: see Denoise.
    std::array<float, kOctaves> nowLimits{};
    std::array<float, kOctaves> heldLimits{};
    /// Where the noise's held level usually sits under its ceilings in each
    /// octave, its rest: what the block measures the noise's drift from.
    std::array<float, kOctaves> rest{};
    double sampleRate = 0.0;
    float seconds = 0.0f;
    bool valid = false;
};

/// Takes a noise out of everything once it has heard the noise on its own
/// (ADR-0015, ADR-0033): a fan or a hiss, and as well the buzzing and
/// ticking a sound card picks up from the computer, which comes and goes.
///
/// **Learning** takes kLearnSeconds. Every band's level is measured twice,
/// as it is in each frame ("now") and as the median of the last
/// kHeldSeconds ("held"). The first half of the time finds, for each band,
/// the level the noise stays at or under in 99 % of its frames, now and
/// held, each band pooled with the two beside it: the noise's ceilings, its
/// loud moments included, not its average. The second half measures, octave
/// by octave, how far the noise alone goes past those ceilings at once, now
/// and held, and where its held level usually rests under them. While it
/// listens the sound passes through untouched.
///
/// **Removing.** A band's excess is how far it is over its ceiling. Each
/// octave adds up what its bands are over by, past kEvidenceDb, now and
/// held, and asks two questions: is it more than the noise ever gave now,
/// and is it more than the noise ever gave held? The first is strict (twice
/// the most, and kNowLimitMargin more), because a noise's ticks give a lot
/// of it; the second is not (half as much again, and kHeldLimitMargin
/// more), because a noise's held level hardly strays, and what lasts past
/// its ceiling is a sound: a quiet note under the ticks, a voice's weaker
/// harmonics. An octave that answers no to both holds **the noise alone**,
/// and all its bands go down by "reduction": the noise's own ticks open
/// nothing. **Octave by octave**, because a noise does not come and go all
/// at once: a tone of the computer's that swells for a moment opens the
/// octave it is in, and not the ticks in the bass. In an octave that holds
/// more, a band passes untouched once its excess, by whichever answer was
/// yes, reaches "threshold", and goes down in full kKneeDb under that. A
/// band opens at once and closes over kReleaseSeconds. Both sides share one
/// gain per band, from their average power: a noise the device picks up is
/// the same on both.
///
/// **Following the noise up.** A computer's noise grows when the computer
/// works harder. In every frame the block measures where each octave's held
/// level rests against where it rested while learning (kRestShare of the
/// octave's bands, so that a tone or two do not move it), and the octave's
/// ceilings follow the difference over kFollowSeconds: up to
/// kMostFollowedDb above what was learned, and never below it, since the
/// noise's ticks need not have quietened when its hiss did. **A noise grown
/// is every band a little over; a sound is some bands a lot over**: an
/// octave follows only while three quarters of its bands are less than
/// kFollowUnderDb over its ceilings, held.
///
/// **The profile is part of the block's state**, saved with the session in
/// "profile", so it survives a restart. It is learned on the audio thread
/// and handed to the thread that saves through a triple buffer, so saving
/// never reads a profile half written. A profile written by the block's
/// first version (an average, "v1") is not read: it has to be learned again.
///
/// **Latency** is a frame, 2048 samples, whatever the settings: reported,
/// so the graph lines other paths up with it.
class Denoise final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.denoise";

    static constexpr std::size_t kFrameSize = 2048;
    static constexpr std::size_t kHop = kFrameSize / 4;
    static constexpr std::size_t kBins = kFrameSize / 2 + 1;
    static constexpr std::size_t kOctaves = DenoiseProfile::kOctaves;
    static_assert(kBins == DenoiseProfile::kBins, "the profile holds a band for every bin");

    /// How long it listens: half to find the ceilings, half to see how far
    /// the noise strays past them.
    static constexpr float kLearnSeconds = 10.0f;
    /// The share of the noise's frames a band's ceiling is at or over.
    static constexpr double kQuantile = 0.99;
    /// How long the held level is the median of: longer than a click,
    /// shorter than a note.
    static constexpr double kHeldSeconds = 0.075;
    /// One more than the most frames the held level is taken over, at any
    /// rate.
    static constexpr std::size_t kMostHeldFrames = 32;
    /// How far over its ceiling a band has to be to count as evidence of
    /// something other than the noise.
    static constexpr float kEvidenceDb = 3.0f;
    /// What an octave has to add up to, now and held: the most the noise
    /// alone gave there while learning, times the factor, and the margin
    /// more (a sum over bands of decibels).
    static constexpr float kNowLimitFactor = 2.0f;
    static constexpr float kNowLimitMargin = 6.0f;
    static constexpr float kHeldLimitFactor = 1.5f;
    static constexpr float kHeldLimitMargin = 1.0f;
    /// How far under "threshold" a band is turned down in full.
    static constexpr float kKneeDb = 6.0f;
    /// How long a band takes to close from open to turned down in full.
    static constexpr double kReleaseSeconds = 0.06;
    /// Following the noise up: the share of an octave's bands its rest is
    /// read at, how long the ceilings take to follow, and how far they go;
    /// and how far over its ceilings a quarter of the octave's bands may be
    /// while it follows.
    static constexpr double kRestShare = 0.25;
    static constexpr double kFollowSeconds = 2.0;
    static constexpr float kMostFollowedDb = 10.0f;
    static constexpr float kFollowUnderDb = 6.0f;
    /// It follows once in this many frames.
    static constexpr std::size_t kFollowEvery = 4;
    /// The lowest level a band is measured at; digital silence is this.
    static constexpr float kLowestDb = -180.0f;

    static constexpr float kDefaultReductionDb = 20.0f;
    static constexpr float kHighestReductionDb = 40.0f;
    static constexpr float kDefaultThresholdDb = 3.0f;
    static constexpr float kHighestThresholdDb = 20.0f;

    /// What the block publishes (rt::BlockMeter): its state (kIdle,
    /// kLearning or kActive), how far through learning it is (0 to 1), and
    /// how many seconds of noise the profile it uses was learned from.
    static constexpr std::size_t kStateValue = 0;
    static constexpr std::size_t kProgressValue = 1;
    static constexpr std::size_t kLearnedSecondsValue = 2;
    static constexpr float kIdle = 0.0f;
    static constexpr float kLearning = 1.0f;
    static constexpr float kActive = 2.0f;

    enum Param : std::uint32_t {
        Reduction = 0, ///< dB: how far the noise is turned down.
        Threshold, ///< dB: how far above the noise's ceiling a band passes untouched.
        Learn, ///< A count; each new value starts learning.
        Forget, ///< A count; each new value forgets the profile.
        Profile, ///< The learned noise, as text; see encodeProfile().
    };

    using NoiseProfile = DenoiseProfile;

    Denoise();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// A profile as the text "profile" holds: its rate, its length, every
    /// band's two ceilings and every octave's limits and rest, to a tenth of
    /// a dB. Empty for no profile.
    [[nodiscard]] static std::string encodeProfile(const NoiseProfile &profile);
    /// The profile \a text holds, or nothing if it is not one this version
    /// reads.
    [[nodiscard]] static std::optional<NoiseProfile> decodeProfile(std::string_view text);

    /// The octave a band at \a frequency Hz is counted in: up to 150 Hz,
    /// then each doubling up to 9.6 kHz, then the rest.
    [[nodiscard]] static std::size_t octaveOf(double frequency) noexcept RB_NONBLOCKING;
    /// How many frames the held level is the median of at \a sampleRate.
    [[nodiscard]] static std::size_t heldFrames(double sampleRate) noexcept;

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// Whether it has a profile to remove, on the audio thread's side.
    [[nodiscard]] bool active() const noexcept RB_NONBLOCKING
    {
        return m_haveProfile;
    }

    [[nodiscard]] bool learning() const noexcept RB_NONBLOCKING
    {
        return m_learning;
    }

    /// Whether every octave of the last frame was taken for the noise
    /// alone: for a test.
    [[nodiscard]] bool heardNoiseAlone() const noexcept RB_NONBLOCKING
    {
        return m_noiseAlone;
    }

    /// How far \a octave's ceilings are following the noise up, in dB: for
    /// a test.
    [[nodiscard]] float followed(std::size_t octave) const noexcept RB_NONBLOCKING
    {
        return octave < kOctaves ? m_followed[octave] : 0.0f;
    }

private:
    static constexpr std::size_t kChannels = 2;
    /// The ceilings are found from a count of every band's levels in steps
    /// of 1 dB, from kLowestDb up.
    static constexpr std::size_t kSteps = 190;

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// The profile the store holds, into the fingerprint the audio thread
    /// uses, at the running rate.
    void adoptStoredProfile();
    /// One frame of both channels: transform, measure, learn or remove,
    /// and back.
    void processFrame() noexcept RB_NONBLOCKING;
    /// Every band's level now, into the held levels' ring, and its held
    /// level.
    void measure() noexcept RB_NONBLOCKING;
    /// One frame's worth of learning.
    void learnFrame() noexcept RB_NONBLOCKING;
    /// The ceilings from the counts of the first half.
    void findCeilings() noexcept RB_NONBLOCKING;
    /// Where \a octave's held level rests under its learned ceilings in
    /// this frame: the kRestShare point of its bands' held excess; and, into
    /// \a upper unless null, the point as far from the top.
    [[nodiscard]] float restOf(std::size_t octave, float *upper) noexcept RB_NONBLOCKING;
    /// Each band's excess over the fingerprint, now and held, and which
    /// octaves hold more than the noise; whether any does.
    [[nodiscard]] bool weighFrame() noexcept RB_NONBLOCKING;
    /// The ceilings of the octaves that hold the noise alone, a step closer
    /// to where the noise is now.
    void followNoise() noexcept RB_NONBLOCKING;
    /// The gains for the frame just weighed.
    void decideGains() noexcept RB_NONBLOCKING;
    void startLearning() noexcept RB_NONBLOCKING;
    void finishLearning() noexcept RB_NONBLOCKING;
    void dropProfile() noexcept RB_NONBLOCKING;
    void publishProfile(bool valid) noexcept RB_NONBLOCKING;
    /// Every band turned down in full, or every band open.
    void closeAll() noexcept RB_NONBLOCKING;
    void openAll() noexcept RB_NONBLOCKING;
    /// The profile the saving thread should write, if the audio thread has
    /// learned or forgotten one since the block was made.
    [[nodiscard]] std::optional<std::string> learnedProfileText() const;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::RealFft m_fft;
    rt::BlockMeter *m_meter = nullptr;
    double m_sampleRate = 48000.0;

    /// The windows, sqrt-Hann, and the overlap-add scale.
    std::vector<float> m_window;
    /// What a band's power is multiplied by to be a level against a
    /// full-scale sine's.
    float m_levelScale = 1.0f;
    /// Per channel: the last frame of input, the overlap-add of output, and
    /// the hop being played out.
    std::array<std::vector<float>, kChannels> m_input;
    std::array<std::vector<float>, kChannels> m_output;
    std::array<std::vector<float>, kChannels> m_ready;
    std::array<std::vector<std::complex<float>>, kChannels> m_spectrum;
    std::vector<float> m_frame;
    std::size_t m_position = kFrameSize - kHop;

    /// Which octave each band is counted in; the first band of each, the
    /// bands of an octave being one run; and how many bands each has.
    std::vector<std::uint8_t> m_octave;
    std::array<std::size_t, kOctaves + 1> m_octaveStart{};
    std::array<std::size_t, kOctaves> m_octaveBands{};

    /// Every band's level in the frame being worked on, now and held; the
    /// last m_heldFrames frames' levels, oldest overwritten first; how far
    /// over its ceilings each band is, now and held; and room to find a
    /// rest in.
    std::vector<float> m_level;
    std::vector<float> m_held;
    std::vector<float> m_ring;
    std::size_t m_heldFrames = 7;
    std::size_t m_ringSlot = 0;
    std::vector<float> m_excessNow;
    std::vector<float> m_excessHeld;
    std::vector<float> m_scratch;

    /// The fingerprint the audio thread removes, at the running rate, and
    /// how far each octave's ceilings are following the noise up.
    std::vector<float> m_ceilingNow;
    std::vector<float> m_ceilingHeld;
    std::array<float, kOctaves> m_nowLimit{};
    std::array<float, kOctaves> m_heldLimit{};
    std::array<float, kOctaves> m_rest{};
    std::array<float, kOctaves> m_followed{};
    float m_followStep = 0.0f;
    std::size_t m_followTick = 0;
    bool m_haveProfile = false;
    float m_profileSeconds = 0.0f;
    /// Which octaves of the frame just weighed hold more than the noise,
    /// now and held, and whether none does.
    std::array<bool, kOctaves> m_heardNow{};
    std::array<bool, kOctaves> m_heardHeld{};
    bool m_noiseAlone = false;

    /// Each band's gain after the release, in dB, and that as a factor.
    std::vector<float> m_gainDb;
    std::vector<float> m_gain;

    /// Learning: each band's levels counted in 1 dB steps, now and held, and
    /// the highest step each band has reached; how many frames each count
    /// holds; the frames so far and in all; and, in the second half, the
    /// most evidence each octave gave, now and held, and its rests added up.
    std::vector<std::uint16_t> m_countNow;
    std::vector<std::uint16_t> m_countHeld;
    std::vector<std::uint8_t> m_topNow;
    std::vector<std::uint8_t> m_topHeld;
    std::size_t m_countedNow = 0;
    std::size_t m_countedHeld = 0;
    std::size_t m_learnFrames = 0;
    std::size_t m_learnTarget = 1;
    std::array<float, kOctaves> m_mostNow{};
    std::array<float, kOctaves> m_mostHeld{};
    std::array<double, kOctaves> m_restSum{};
    std::size_t m_restFrames = 0;
    bool m_learning = false;
    float m_seenLearn = 0.0f;
    float m_seenForget = 0.0f;

    /// What the loaded state said the profile was, as numbers.
    NoiseProfile m_stored;
    /// Profiles learned or forgotten on the audio thread, for saveState().
    mutable rt::TripleBuffer<NoiseProfile> m_published;
    /// How many have been published, counted by the audio thread and read
    /// by saveState() only as "any at all".
    std::atomic<std::uint32_t> m_publishedCount{0};
    bool m_prepared = false;
};

} // namespace rb::blocks
