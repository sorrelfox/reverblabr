// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A signal generator for testing equipment and anything else that needs a
/// known sound: a tone or noise, at a frequency and a level, on the
/// left, the right or both.
///
/// **Listed under Outputs, wired like any other block.** It takes nothing in
/// and has an output connector, so what it makes can go through a meter, a
/// volume or anything else before an Output Device plays it. Its category is
/// where the library shows it; that no audio system fills its ports is what
/// the graph goes by (BlockInfo::inputsFromAudioSystem).
///
/// **The level is the waveform's peak**, in dBFS, and it starts at −18: a
/// test tone at full scale is a speaker or an ear put at risk by a slip, so
/// it is loud only when somebody asks for loud. For noise it is the nominal
/// peak, and the rare sample of pink noise past full scale is held at it.
///
/// **Nothing it does clicks.** The level, the frequency and which side the
/// signal is on all glide, and switching the block off and on is the
/// graph's cross-fade to silence, as it is for any block with nothing to
/// pass through.
///
/// **Square, triangle and saw are band-limited** (polyBLEP and polyBLAMP), so
/// a high tone does not fold back into the audible range as a second tone
/// that is not there.
class Wave final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "output.wave";

    /// The stable ids of the waveforms, in the order they are offered.
    static constexpr std::string_view kSine = "sine";
    static constexpr std::string_view kSquare = "square";
    static constexpr std::string_view kTriangle = "triangle";
    static constexpr std::string_view kSaw = "saw";
    static constexpr std::string_view kWhiteNoise = "white";
    static constexpr std::string_view kPinkNoise = "pink";

    /// The stable ids of where the signal goes.
    static constexpr std::string_view kBoth = "both";
    static constexpr std::string_view kLeft = "left";
    static constexpr std::string_view kRight = "right";

    static constexpr float kDefaultFrequency = 1000.0f;
    static constexpr float kMinimumFrequency = 20.0f;
    static constexpr float kMaximumFrequency = 20000.0f;
    static constexpr float kDefaultLevelDb = -18.0f;
    static constexpr float kMinimumLevelDb = -80.0f;
    static constexpr float kMaximumLevelDb = 0.0f;
    /// How long a change of level, frequency or side takes to arrive: long
    /// enough not to step the signal, short enough not to be a delay.
    static constexpr float kSmoothingMs = 20.0f;

    enum Param : std::uint32_t {
        Waveform = 0,
        Frequency,
        Level,
        Channels,
    };

    /// The waveforms in the order the enum offers them, which is the order
    /// ParamStore::number() counts them in.
    enum class Shape : std::uint8_t {
        Sine = 0,
        Square,
        Triangle,
        Saw,
        WhiteNoise,
        PinkNoise,
    };

    Wave();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

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

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    /// The next sample of the current shape, between −1 and 1, before the
    /// level. Advances the phase or the noise.
    [[nodiscard]] float nextSample(Shape shape, double increment) noexcept RB_NONBLOCKING;
    /// Where the level and each side's gain are heading, from the store.
    void aimSmoothers() noexcept RB_NONBLOCKING;
    /// Starts every smoother where it is heading, for a start with no glide.
    void settleSmoothers() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    dsp::LinearSmoother m_level;
    dsp::LinearSmoother m_frequency;
    dsp::LinearSmoother m_left;
    dsp::LinearSmoother m_right;

    double m_sampleRate = 48000.0;
    /// Where in its cycle the oscillator is, from 0 up to 1.
    double m_phase = 0.0;
    /// The state of the noise generator, which must never be zero.
    std::uint32_t m_noise = 0x9e3779b9U;
    /// The pink filter's memory.
    std::array<float, 7> m_pink{};
    bool m_prepared = false;
};

} // namespace rb::blocks
