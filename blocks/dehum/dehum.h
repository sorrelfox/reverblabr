// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/biquad.h"
#include "dsp/block.h"
#include "dsp/humtracker.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Takes out the hum a mains supply leaves in a recording, and the whine
/// its harmonics make above it.
///
/// **A notch at the base frequency and one at each harmonic**, up to the
/// tenth: a cascade of narrow notches (dsp::Svf, FilterShape::Notch), so the
/// hum goes and what lies between its harmonics — a voice, an instrument —
/// stays. The base is 50 or 60 Hz and can be tuned 2 Hz either way, for a
/// supply that runs a little off or a tape that does.
///
/// **Narrow, and narrower towards the bottom.** Each notch is kWidthHz wide
/// at the base and wider by the square root of its harmonic's number, so the
/// bottom one takes 2 Hz out and the tenth about 6 — wide enough to catch a
/// harmonic that has drifted with its base, narrow enough that a note half
/// way between two harmonics is left within a tenth of a decibel.
///
/// **Amount is how deep the notches go**, from 0 (nothing: the signal passes
/// untouched, bit for bit) to 10 (60 dB down at every harmonic). It works by
/// mixing each notch's output with its input, so a notch half in is a dip
/// rather than a hole, and turning it glides.
///
/// **Auto** follows a hum whose base drifts, a supply running off or a
/// tape running at the wrong speed, without anybody moving Base: a
/// dsp::HumTracker locks onto the fundamental, cross-checked against the 2nd
/// and 3rd harmonics, and the notches follow it. Base is then where it starts
/// looking, and where the notches wait while there is no hum to lock onto.
/// The notches are a little wider with Auto on (kAutoWidthHz), room for what
/// the tracker is still off by while the hum is on the move. With Auto off
/// the block is exactly as it was.
class Dehum final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.dehum";

    static constexpr std::size_t kMaximumHarmonics = 10;
    static constexpr float kMinimumBase = 48.0f;
    static constexpr float kMaximumBase = 62.0f;
    static constexpr float kDefaultBase = 50.0f;
    static constexpr float kMaximumAmount = 10.0f;
    static constexpr float kDefaultAmount = 5.0f;
    /// How deep each step of Amount goes: 6 dB, so 10 is 60 dB down.
    static constexpr float kDepthPerStepDb = 6.0f;
    /// How wide the notch at the base frequency is, between its half-power
    /// points.
    static constexpr double kWidthHz = 2.0;
    /// How wide the notch at the base is with Auto on: the tracker is at
    /// most about 10 mHz off while a hum drifts, and at the tenth harmonic
    /// that is 0.1 Hz; a notch this wide still takes that 30 dB down.
    static constexpr double kAutoWidthHz = 3.0;
    /// How long a change of base or amount takes to arrive.
    static constexpr float kSmoothingMs = 30.0f;
    /// How often, in samples, the notches are moved to what Auto tracks.
    static constexpr std::uint32_t kAutoDesignFrames = 16;
    /// What the block publishes (rt::BlockMeter::publish): the base the
    /// notches are on, in Hz, and with Auto on whether the tracker is locked
    /// (1) or holding (0); with Auto off, −1.
    static constexpr std::size_t kBaseValue = 0;
    static constexpr std::size_t kLockedValue = 1;

    enum Param : std::uint32_t {
        Base = 0, ///< 48 … 62 Hz.
        Amount, ///< 0 … 10.
        Harmonics, ///< 1 … 10: how many notches, the base being the first.
        Auto, ///< Follow the hum's base; off by default.
    };

    Dehum();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// The notch that takes out harmonic \a number (1 is the base) of
    /// \a base Hz, \a width wide at the base.
    [[nodiscard]] static dsp::FilterSpec notch(double base, std::size_t number, double width = kWidthHz) noexcept;

    /// How much of a notch's output is heard at \a amount: 1 − 10^(−depth/20).
    [[nodiscard]] static double mixFor(double amount) noexcept;

    /// The response at \a frequency, in dB, for these settings: what a test
    /// holds the block to.
    [[nodiscard]] static double magnitudeDb(double frequency,
                                            double base,
                                            double amount,
                                            std::size_t harmonics,
                                            double sampleRate,
                                            double width = kWidthHz) noexcept;

    /// The tracker behind Auto: for a test.
    [[nodiscard]] const dsp::HumTracker &tracker() const noexcept
    {
        return m_tracker;
    }

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
    static constexpr std::uint32_t kChannels = 2;

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// The notches' coefficients for \a base Hz, \a width wide.
    void design(double base, double width) noexcept RB_NONBLOCKING;
    /// Auto switched on or off since the last cycle.
    void followAuto() noexcept RB_NONBLOCKING;
    /// How many notches the store asks for, held under half the rate.
    [[nodiscard]] std::size_t wantedHarmonics() const noexcept RB_NONBLOCKING;
    void aimSmoothers() noexcept RB_NONBLOCKING;
    void settleSmoothers() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    double m_sampleRate = 48000.0;

    dsp::LinearSmoother m_base;
    /// The notches' width at the base: kWidthHz, or kAutoWidthHz with Auto.
    dsp::LinearSmoother m_width;
    dsp::HumTracker m_tracker;
    /// Whether the notches are following the tracker.
    bool m_tracking = false;
    /// Base as last seen with Auto on, so a move of it re-aims the tracker.
    float m_aimed = kDefaultBase;
    std::uint32_t m_designCount = 0;
    rt::BlockMeter *m_meter = nullptr;
    /// Each notch's mix; a harmonic being switched on or off fades.
    std::array<dsp::LinearSmoother, kMaximumHarmonics> m_mix{};
    std::array<dsp::SvfCoefficients, kMaximumHarmonics> m_notches{};
    std::array<std::array<dsp::Svf, kMaximumHarmonics>, kChannels> m_filters{};
    bool m_prepared = false;
};

} // namespace rb::blocks
