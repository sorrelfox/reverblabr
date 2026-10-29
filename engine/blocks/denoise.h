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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A noise the Denoise block has learned: the power of every band of a
/// 2048-point spectrum, and the rate and the time it was learned over.
struct DenoiseProfile {
    static constexpr std::size_t kBins = 1025;

    std::array<float, kBins> power{};
    double sampleRate = 0.0;
    float seconds = 0.0f;
    bool valid = false;
};

/// Takes a steady noise out of everything once it has heard the noise on
/// its own (ADR-0015).
///
/// **Learning.** Asked to learn, it listens for kLearnSeconds and averages
/// the noise's power in every one of the 1025 bands of a 2048-point
/// spectrum: the noise profile. While it listens the sound passes through
/// untouched, so what is being learned is what is heard.
///
/// **Removing.** The signal is cut into 2048-sample frames, a quarter of
/// one apart, each windowed and transformed. A band whose power is close
/// to the learned noise's is turned down, as far as "reduction" allows; a
/// band well above it is left alone. The gain for a band is
///
///     max(floor, 1 - k * noise / power)
///
/// with the floor from "reduction" and k from "threshold", how far above
/// the noise a band has to be to pass untouched. Both sides share one gain
/// per band, from their average power, so the picture does not wander; and
/// the power and the gains are smoothed from frame to frame, which keeps the
/// noise that is left from twinkling ("musical noise").
///
/// **The profile is part of the block's state**, saved with the session in
/// "profile", so it survives a restart. It is learned on the audio thread
/// and handed to the thread that saves through a triple buffer, so saving
/// never reads a profile half written.
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
    static_assert(kBins == DenoiseProfile::kBins, "the profile holds a band for every bin");
    static constexpr float kLearnSeconds = 3.0f;

    static constexpr float kDefaultReductionDb = 20.0f;
    static constexpr float kHighestReductionDb = 30.0f;
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
        Reduction = 0, ///< dB: the most a band is turned down.
        Threshold, ///< dB: how far above the noise a band passes untouched.
        Learn, ///< A count; each new value starts learning.
        Forget, ///< A count; each new value forgets the profile.
        Profile, ///< The learned noise, as text; see encodeProfile().
    };

    using NoiseProfile = DenoiseProfile;

    Denoise();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// A profile as the text "profile" holds: its rate, its length and the
    /// power of every band in dB to a tenth. Empty for no profile.
    [[nodiscard]] static std::string encodeProfile(const NoiseProfile &profile);
    /// The profile \a text holds, or nothing if it is not one.
    [[nodiscard]] static std::optional<NoiseProfile> decodeProfile(std::string_view text);

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

private:
    static constexpr std::size_t kChannels = 2;

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// The profile the store holds, into the one the audio thread uses, at
    /// the running rate.
    void adoptStoredProfile();
    /// One frame of both channels: transform, learn or remove, and back.
    void processFrame() noexcept RB_NONBLOCKING;
    void finishLearning() noexcept RB_NONBLOCKING;
    void publishProfile(bool valid) noexcept RB_NONBLOCKING;
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
    /// Per channel: the last frame of input, the overlap-add of output, and
    /// the hop being played out.
    std::array<std::vector<float>, kChannels> m_input;
    std::array<std::vector<float>, kChannels> m_output;
    std::array<std::vector<float>, kChannels> m_ready;
    std::array<std::vector<std::complex<float>>, kChannels> m_spectrum;
    std::vector<float> m_frame;
    std::size_t m_position = kFrameSize - kHop;

    /// The noise the audio thread removes, per band, at the running rate.
    std::vector<float> m_noise;
    /// The gain per band, smoothed from frame to frame.
    std::vector<float> m_gains;
    /// The average power of both sides per band, smoothed from frame to
    /// frame.
    std::vector<float> m_power;
    bool m_haveProfile = false;
    float m_profileSeconds = 0.0f;

    /// Learning: the power summed so far and how many frames.
    std::vector<double> m_learnSum;
    std::size_t m_learnFrames = 0;
    std::size_t m_learnTarget = 1;
    bool m_learning = false;
    float m_seenLearn = 0.0f;
    float m_seenForget = 0.0f;

    /// What the loaded state said the profile was, as text, and as numbers.
    NoiseProfile m_stored;
    /// Profiles learned or forgotten on the audio thread, for saveState().
    mutable rt::TripleBuffer<NoiseProfile> m_published;
    /// How many have been published, counted by the audio thread and read
    /// by saveState() only as "any at all".
    std::atomic<std::uint32_t> m_publishedCount{0};
    bool m_prepared = false;
};

} // namespace rb::blocks
