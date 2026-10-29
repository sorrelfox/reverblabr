// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/hearingbands.h"

#include "dsp/erbscale.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rb::dsp
{

float steadyGainDb(const HearingBand &band, const HearingSettings &settings, float levelDbfs) noexcept RB_NONBLOCKING
{
    // **A louder sound never comes out quieter.** Gains that fall faster
    // than the level rises (average 40 dB above loud at 10) would turn a loud
    // sound down below an average one; each step is held at -1 dB a decibel,
    // which leaves the output level instead.
    const float soft = band.softGainDb;
    const float average = std::max(band.averageGainDb, soft - kLevelStepDb);
    const float loud = std::max(band.loudGainDb, average - kLevelStepDb);
    float gain = soft;
    if (levelDbfs > kLoudInputDbfs) {
        // Above the loud level the line from average to loud carries on, as
        // long as it slopes down.
        const float slope = std::min((loud - average) / kLevelStepDb, 0.0f);
        gain = loud + slope * (levelDbfs - kLoudInputDbfs);
    } else if (levelDbfs > kAverageInputDbfs) {
        const float t = (levelDbfs - kAverageInputDbfs) / kLevelStepDb;
        gain = average + t * (loud - average);
    } else if (levelDbfs > kSoftInputDbfs) {
        const float t = (levelDbfs - kSoftInputDbfs) / kLevelStepDb;
        gain = soft + t * (average - soft);
    }
    gain += settings.loudnessDb;
    if (levelDbfs < settings.noiseFloorDbfs) {
        // The lift falls away below the floor, to none; a cut stays a cut.
        gain = std::max(std::min(gain, 0.0f), gain - (settings.noiseFloorDbfs - levelDbfs));
    }
    gain = std::min(gain, settings.maximumGainDb);
    return std::min(gain, band.ceilingDbfs - levelDbfs);
}

void BandLayout::layOut(std::span<const double> channelCentresHz, std::size_t bands, double couplingErbs)
{
    m_centres = bands == channelCentresHz.size() ? std::vector<double>(channelCentresHz.begin(), channelCentresHz.end())
                                                 : erbCentres(bands);
    const std::size_t channels = channelCentresHz.size();
    m_lowerBand.assign(channels, 0);
    m_upperBand.assign(channels, 0);
    m_upperShare.assign(channels, 0.0f);
    m_levelScale.assign(channels, 1.0f);
    m_fine.assign(channels, {});
    m_coupledFrom.assign(channels, 0);
    m_coupledTo.assign(channels, 0);
    m_fineLevel.assign(channels, 0.0f);
    m_bandChannel.assign(bands, 0);
    std::vector<double> channelErbs(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        channelErbs[channel] = erbNumber(channelCentresHz[channel]);
    }
    std::vector<double> bandErbs(bands);
    for (std::size_t band = 0; band < bands; ++band) {
        bandErbs[band] = erbNumber(m_centres[band]);
        std::size_t nearest = 0;
        for (std::size_t channel = 1; channel < channels; ++channel) {
            if (std::abs(channelErbs[channel] - bandErbs[band]) < std::abs(channelErbs[nearest] - bandErbs[band])) {
                nearest = channel;
            }
        }
        m_bandChannel[band] = nearest;
    }
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const double at = channelErbs[channel];
        if (bands == 0) {
            break;
        }
        if (at <= bandErbs.front()) {
            m_lowerBand[channel] = 0;
            m_upperBand[channel] = 0;
        } else if (at >= bandErbs.back()) {
            m_lowerBand[channel] = bands - 1;
            m_upperBand[channel] = bands - 1;
        } else {
            for (std::size_t band = 1; band < bands; ++band) {
                if (at <= bandErbs[band]) {
                    m_lowerBand[channel] = band - 1;
                    m_upperBand[channel] = band;
                    m_upperShare[channel] =
                        static_cast<float>((at - bandErbs[band - 1]) / (bandErbs[band] - bandErbs[band - 1]));
                    break;
                }
            }
        }
        m_coupledFrom[channel] = channel;
        m_coupledTo[channel] = channel;
        for (std::size_t other = 0; other < channels; ++other) {
            const double distance = std::abs(channelErbs[other] - at);
            if (distance < kFineEdgeErbs) {
                const double weight =
                    distance <= kFineFlatErbs ? 1.0 : (kFineEdgeErbs - distance) / (kFineEdgeErbs - kFineFlatErbs);
                m_fine[channel].emplace_back(other, static_cast<float>(weight));
            }
            if (distance <= couplingErbs) {
                m_coupledFrom[channel] = std::min(m_coupledFrom[channel], other);
                m_coupledTo[channel] = std::max(m_coupledTo[channel], other);
            }
        }
    }
}

void BandLayout::channelLevels(std::span<const float> channelPower, std::span<float> levelPower) noexcept RB_NONBLOCKING
{
    const std::size_t channels = std::min(levelPower.size(), m_fine.size());
    for (std::size_t channel = 0; channel < channels; ++channel) {
        float sum = 0.0f;
        for (const auto &[other, weight] : m_fine[channel]) {
            if (other < channelPower.size()) {
                sum += weight * channelPower[other];
            }
        }
        m_fineLevel[channel] = sum * m_levelScale[channel];
    }
    for (std::size_t channel = 0; channel < channels; ++channel) {
        float loudest = 0.0f;
        for (std::size_t other = m_coupledFrom[channel]; other <= m_coupledTo[channel] && other < channels; ++other) {
            loudest = std::max(loudest, m_fineLevel[other]);
        }
        levelPower[channel] = loudest;
    }
}

void BandLayout::channelSettings(std::span<const HearingBand> bands,
                                 std::span<HearingBand> channels) const noexcept RB_NONBLOCKING
{
    const std::size_t count = std::min(channels.size(), m_lowerBand.size());
    if (bands.empty()) {
        return;
    }
    for (std::size_t channel = 0; channel < count; ++channel) {
        const HearingBand &lower = bands[std::min(m_lowerBand[channel], bands.size() - 1)];
        const HearingBand &upper = bands[std::min(m_upperBand[channel], bands.size() - 1)];
        const float t = m_upperShare[channel];
        const auto between = [t](float a, float b) noexcept RB_NONBLOCKING { return a + t * (b - a); };
        channels[channel] = HearingBand{
            .softGainDb = between(lower.softGainDb, upper.softGainDb),
            .averageGainDb = between(lower.averageGainDb, upper.averageGainDb),
            .loudGainDb = between(lower.loudGainDb, upper.loudGainDb),
            .ceilingDbfs = between(lower.ceilingDbfs, upper.ceilingDbfs),
        };
    }
}

void bandsFromPoints(std::span<const HearingPoint> points,
                     std::span<const double> centresHz,
                     const HearingBand &fallback,
                     std::span<HearingBand> bands) noexcept RB_NONBLOCKING
{
    // The points that are on, low to high (an insertion sort, as there are
    // at most kMaxHearingPoints and it allocates nothing).
    std::array<std::size_t, kMaxHearingPoints> order{};
    std::array<double, kMaxHearingPoints> where{};
    std::size_t count = 0;
    for (std::size_t point = 0; point < points.size() && count < kMaxHearingPoints; ++point) {
        if (!points[point].on || !(points[point].hz > 0.0)) {
            continue;
        }
        const double at = std::log(points[point].hz);
        std::size_t slot = count;
        while (slot > 0 && where[slot - 1] > at) {
            where[slot] = where[slot - 1];
            order[slot] = order[slot - 1];
            --slot;
        }
        where[slot] = at;
        order[slot] = point;
        ++count;
    }
    const std::size_t bandCount = std::min(centresHz.size(), bands.size());
    if (count == 0) {
        std::fill_n(bands.begin(), bandCount, fallback);
        return;
    }
    // One point to a frequency: the first of any on the same one.
    constexpr double kSameFrequency = 1.0e-9;
    std::array<double, kMaxHearingPoints> x{};
    std::array<std::array<float, kMaxHearingPoints>, 4> y{};
    std::size_t kept = 0;
    for (std::size_t slot = 0; slot < count; ++slot) {
        if (kept > 0 && where[slot] - x[kept - 1] < kSameFrequency) {
            continue;
        }
        const HearingBand &settings = points[order[slot]].settings;
        x[kept] = where[slot];
        y[0][kept] = settings.softGainDb;
        y[1][kept] = settings.averageGainDb;
        y[2][kept] = settings.loudGainDb;
        y[3][kept] = settings.ceilingDbfs;
        ++kept;
    }
    std::array<MonotoneCurve, 4> curves;
    for (std::size_t which = 0; which < curves.size(); ++which) {
        curves[which].fit(std::span<const double>{x.data(), kept}, std::span<const float>{y[which].data(), kept});
    }
    for (std::size_t band = 0; band < bandCount; ++band) {
        const double at = std::log(centresHz[band]);
        bands[band] = HearingBand{
            .softGainDb = curves[0].at(at),
            .averageGainDb = curves[1].at(at),
            .loudGainDb = curves[2].at(at),
            .ceilingDbfs = curves[3].at(at),
        };
    }
}

void BandGainControl::prepare(std::size_t count, double periodSeconds)
{
    m_period = periodSeconds;
    m_fast.assign(count, 0.0f);
    m_slow.assign(count, 0.0f);
    m_inBurst.assign(count, 0);
    m_levels.assign(count, -120.0f);
    m_holdUpdates = static_cast<int>(kBurstHoldSeconds / periodSeconds);
    m_fadeStep = static_cast<float>(periodSeconds / kFadeInSeconds);
    setSpeed(HearingSpeed::Adaptive);
    reset(HearingSettings{});
}

void BandGainControl::setSpeed(HearingSpeed speed) noexcept RB_NONBLOCKING
{
    m_speed = speed;
    // The detectors follow the level in dB, so a time constant is a time to
    // settle whatever the size of the step. On the standard 35 dB step at
    // 3:1 the gain is within 3 dB after 2.05 time constants of attack, and
    // within 4 dB after 1.76 of release: these give each speed its
    // attack and release measured that way.
    const auto coefficient = [this](double seconds) noexcept RB_NONBLOCKING {
        return static_cast<float>(std::exp(-m_period / seconds));
    };
    m_fastAttack = coefficient(0.005 / 2.05);
    m_fastRelease = coefficient(0.070 / 1.76);
    m_slowAttack = coefficient((speed == HearingSpeed::Adaptive ? 0.325 : 0.050) / 2.05);
    m_slowRelease = coefficient((speed == HearingSpeed::Adaptive ? 1.0 : 1.5) / 1.76);
}

void BandGainControl::reset(const HearingSettings &settings) noexcept RB_NONBLOCKING
{
    const float floor = settings.noiseFloorDbfs - kDetectorRangeDb;
    std::fill(m_fast.begin(), m_fast.end(), floor);
    std::fill(m_slow.begin(), m_slow.end(), floor);
    std::fill(m_inBurst.begin(), m_inBurst.end(), 0);
    std::fill(m_levels.begin(), m_levels.end(), floor);
    m_fade = 0.0f;
}

void BandGainControl::update(std::span<const float> levelPower,
                             std::span<const HearingBand> bands,
                             const HearingSettings &settings,
                             std::span<float> gainDb) noexcept RB_NONBLOCKING
{
    if (settings.speed != m_speed) {
        setSpeed(settings.speed);
    }
    m_fade = std::min(1.0f, m_fade + m_fadeStep);
    const float floor = settings.noiseFloorDbfs - kDetectorRangeDb;
    const std::size_t count = std::min({levelPower.size(), bands.size(), gainDb.size(), m_fast.size()});
    for (std::size_t band = 0; band < count; ++band) {
        const float now = std::max(10.0f * std::log10(levelPower[band] + 1.0e-12f), floor);
        const auto follow = [now](float &envelope, float attack, float release) noexcept RB_NONBLOCKING {
            const float c = now > envelope ? attack : release;
            envelope = c * envelope + (1.0f - c) * now;
        };
        follow(m_fast[band], m_fastAttack, m_fastRelease);
        const bool burst = m_speed == HearingSpeed::Adaptive && m_fast[band] > m_slow[band] + kBurstDb;
        m_inBurst[band] = burst ? m_inBurst[band] + 1 : 0;
        if (!burst || m_inBurst[band] > m_holdUpdates) {
            follow(m_slow[band], m_slowAttack, m_slowRelease);
        }
        float level = m_fast[band];
        if (m_speed == HearingSpeed::Slow || (m_speed == HearingSpeed::Adaptive && !burst)) {
            level = m_slow[band];
        }
        m_levels[band] = level;
        float gain = steadyGainDb(bands[band], settings, level);
        // **The ceiling has no attack time.** A sound louder than the
        // detector has caught up with is held under it from the stretch it
        // arrives in, which is before it leaves the filterbank's delay.
        gain = std::min(gain, bands[band].ceilingDbfs - now);
        // The fade brings the lift in; a cut, which only ever makes the
        // sound safer, is there from the start.
        gainDb[band] = gain > 0.0f ? gain * m_fade : gain;
    }
}

} // namespace rb::dsp
