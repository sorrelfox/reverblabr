// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/voicelikelihood.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rb::dsp
{

void LevelAndRhythm::prepare(double sampleRate)
{
    m_highCoefficients = designSvf({FilterShape::HighPass, kBandLowHz, kButterworthQ, 0.0}, sampleRate);
    m_lowCoefficients = designSvf({FilterShape::LowPass, kBandHighHz, kButterworthQ, 0.0}, sampleRate);
    m_rhythmHighCoefficients = designSvf({FilterShape::HighPass, kRhythmLowHz, kButterworthQ, 0.0}, kFramesPerSecond);
    m_rhythmLowCoefficients = designSvf({FilterShape::LowPass, kRhythmHighHz, kButterworthQ, 0.0}, kFramesPerSecond);
    const float frameMs = static_cast<float>(1000.0 / kFramesPerSecond);
    m_speechLevel.prepare(sampleRate, frameMs);
    m_wholeLevel.prepare(sampleRate, frameMs);
    m_swing.prepare(kFramesPerSecond, kRhythmWindowMs);
    reset();
}

void LevelAndRhythm::reset() noexcept RB_NONBLOCKING
{
    m_highPass.reset();
    m_lowPass.reset();
    m_rhythmHighPass.reset();
    m_rhythmLowPass.reset();
    m_speechLevel.reset();
    m_wholeLevel.reset();
    m_swing.reset();
    m_floorDb = kNoFloorDb;
    m_lastBandDb = kLowestBandDb;
    m_earlierBandDb = kLowestBandDb;
    m_lastedFrames = 0;
}

float LevelAndRhythm::closeFrame() noexcept RB_NONBLOCKING
{
    const float speech = m_speechLevel.level();
    const float whole = m_wholeLevel.level();
    const float bandDb = std::max(gainToDb(speech), kLowestBandDb);
    // **Only sound teaches the floor.** Digital silence is a program with
    // nothing to play, not a quiet room: taken as the floor, it would leave
    // half a minute's climb before the room's noise stopped reading as a
    // sound standing over it. A frame the silence cuts into is no measure
    // either, so a frame teaches one frame late, once the frames on both
    // sides of it are known to be sound.
    if (m_earlierBandDb > kLowestBandDb && m_lastBandDb > kLowestBandDb && bandDb > kLowestBandDb) {
        m_floorDb = m_lastBandDb < m_floorDb ? m_lastBandDb : m_floorDb + kFloorClimbDb;
    }
    m_earlierBandDb = m_lastBandDb;
    m_lastBandDb = bandDb;
    const float aboveDb = bandDb - m_floorDb;
    const float share = whole > 0.0f ? speech / whole : 0.0f;
    // Measured from the lowest level there is, so that a start in silence
    // is no step for the rhythm's filters: they would ring for half a
    // second over nothing.
    const float highPassed = m_rhythmHighPass.process(bandDb - kLowestBandDb, m_rhythmHighCoefficients);
    const float moved = m_rhythmLowPass.process(highPassed, m_rhythmLowCoefficients);
    m_swing.push(moved);
    m_lastedFrames = aboveDb >= kOpenAboveDb ? std::min(m_lastedFrames + 1, kSteadyFrames) : 0;

    if (share < kLeastBandShare || (m_lastedFrames >= kSteadyFrames && m_swing.level() < kLeastSwingDb)) {
        return 0.0f;
    }
    return std::clamp((aboveDb - kFadeInDb) / kFadeSpanDb, 0.0f, 1.0f);
}

void VoiceLikelihood::prepare(double sampleRate, std::unique_ptr<VoiceModel> model)
{
    m_frameSamples =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(kFrameMs * 1.0e-3 * sampleRate)));
    if (!model) {
        m_modelState = VoiceModelState::NotInThisBuild;
    } else if (sampleRate != VoiceModel::kRate) {
        m_modelState = VoiceModelState::NeedsModelRate;
        model.reset();
    } else {
        m_modelState = VoiceModelState::Ready;
    }
    m_model = std::move(model);
    m_levelAndRhythm.prepare(sampleRate);
    reset();
}

void VoiceLikelihood::reset() noexcept RB_NONBLOCKING
{
    m_fill = 0;
    m_likelihood = 0.0f;
    takeUp(detectorToRun());
}

void VoiceLikelihood::takeUp(VoiceDetector detector) noexcept RB_NONBLOCKING
{
    m_detector = detector;
    if (detector == VoiceDetector::Model) {
        m_model->restart();
    } else {
        m_levelAndRhythm.reset();
    }
}

void VoiceLikelihood::closeFrame() noexcept RB_NONBLOCKING
{
    if (m_detector == VoiceDetector::Model) {
        m_likelihood = std::clamp(m_model->hear(m_frame), 0.0f, 1.0f);
    } else {
        m_likelihood = m_levelAndRhythm.closeFrame();
    }
    if (const VoiceDetector wanted = detectorToRun(); wanted != m_detector) {
        takeUp(wanted);
    }
}

} // namespace rb::dsp
