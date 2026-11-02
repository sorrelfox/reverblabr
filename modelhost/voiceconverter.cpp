// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "voiceconverter.h"

#include "model/voice.h"
#include "onnxrunner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <vector>

namespace rb::modelhost
{

namespace
{

using namespace rb::model;

[[nodiscard]] bool allFinite(std::span<const float> values)
{
    return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
}

/// A float tensor's values, once its element type and size are checked.
[[nodiscard]] std::span<const float> floats(const Ort::Value &value, std::size_t atLeast)
{
    const Ort::TensorTypeAndShapeInfo info = value.GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || info.GetElementCount() < atLeast) {
        return {};
    }
    return {value.GetTensorData<float>(), info.GetElementCount()};
}

/// A tensor's shape with the sizes of one left out, so a batch of one reads
/// the same wherever the export put it.
[[nodiscard]] std::vector<std::int64_t> withoutOnes(std::vector<std::int64_t> shape)
{
    std::erase(shape, std::int64_t{1});
    return shape;
}

} // namespace

struct VoiceConverter::Private {
    Description description;
    Ort::Env env{ORT_LOGGING_LEVEL_FATAL, "reverblabr-model-host"};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::optional<Ort::Session> content;
    std::optional<Ort::Session> pitch;
    std::optional<Ort::Session> voice;
    Refusal refusal = Refusal::None;
    Device device = Device::Cpu;

    /// How many frames a window comes to, what of that the voice hears, and
    /// how many a hop moves on.
    std::uint32_t windowFrames = 0;
    std::uint32_t voiceFrames = 0;
    std::uint32_t hopFrames = 0;

    std::vector<std::int64_t> soundShape;
    std::vector<std::int64_t> mask;
    std::vector<float> spectrum;
    std::vector<float> bands;
    std::size_t bandFrames = 0;
    std::size_t paddedBandFrames = 0;
    std::vector<float> pitches;
    std::vector<float> features;
    std::vector<std::int64_t> steps;
    std::vector<float> hertz;

    /// The noise of the frames the voice is given, moved on by a hop a run.
    std::vector<float> noise;
    std::vector<float> noiseInput;
    std::mt19937 random;
    std::normal_distribution<float> normal;

    /// The end of the last run's sound, for this run to fade from.
    std::vector<float> tail;
    std::vector<float> fade;
    float cents = 0.0f;
    VoiceBands analyser;

    void open(const std::string &folder)
    {
        const VoiceSet &set = description.voice;
        const std::uint32_t threads = description.threads;
        content.emplace(env, (folder + "/" + set.contentFile).c_str(), sessionOptions(threads, device));
        pitch.emplace(env, (folder + "/" + set.pitchFile).c_str(), sessionOptions(threads, device));
        voice.emplace(env, (folder + "/" + description.modelFile).c_str(), sessionOptions(threads, device));

        windowFrames = voiceFramesIn(description.frameSize);
        voiceFrames = std::min(set.voiceFrames, windowFrames);
        hopFrames = description.hopSize / kVoiceFrame;
        const std::uint32_t kept = description.hopOut() + set.crossfade;
        if (voiceFrames <= set.tailFrames || (voiceFrames - set.tailFrames) * set.samplesPerFrame < kept) {
            refusal = Refusal::WrongShape;
            return;
        }

        // What the encoder's sound input declares says how it wants the
        // window: with a dimension for the channel, or without.
        Ort::AllocatorWithDefaultOptions allocator;
        const std::int64_t samples = description.frameSize;
        for (std::size_t index = 0; index < content->GetInputCount(); ++index) {
            const Ort::AllocatedStringPtr name = content->GetInputNameAllocated(index, allocator);
            if (std::string_view{name.get()} == set.contentInput) {
                const std::size_t rank = content->GetInputTypeInfo(index).GetTensorTypeAndShapeInfo().GetShape().size();
                soundShape =
                    rank == 3 ? std::vector<std::int64_t>{1, 1, samples} : std::vector<std::int64_t>{1, samples};
            }
        }
        if (soundShape.empty()) {
            refusal = Refusal::WrongShape;
            return;
        }
        mask.assign(description.frameSize, 1);

        bandFrames = VoiceBands::frames(description.frameSize);
        paddedBandFrames = (bandFrames + kVoiceBandFrames - 1) / kVoiceBandFrames * kVoiceBandFrames;
        bands.assign(paddedBandFrames * kVoiceBands, 0.0f);
        features.resize(std::size_t{voiceFrames} * set.contentBands);
        steps.resize(voiceFrames);
        hertz.resize(voiceFrames);
        pitches.resize(windowFrames);
        noise.resize(std::size_t{voiceFrames} * kVoiceNoiseSize);
        tail.resize(set.crossfade);
        fade.resize(set.crossfade);
        for (std::size_t index = 0; index < fade.size(); ++index) {
            const double position = (static_cast<double>(index) + 0.5) / static_cast<double>(fade.size());
            const double rise = std::sin(0.5 * std::numbers::pi * position);
            fade[index] = static_cast<float>(rise * rise);
        }
        clear();
    }

    void clear()
    {
        random.seed(std::mt19937::default_seed);
        normal.reset();
        std::ranges::generate(noise, [this] { return normal(random); });
        std::ranges::fill(tail, 0.0f);
    }

    /// What is being said, into features. False when the encoder gave fewer
    /// frames than the window has, or other sizes. \a wholeNumbers says
    /// whether all it gave was a number.
    [[nodiscard]] bool hearContent(std::span<const float> sound, bool &wholeNumbers)
    {
        const VoiceSet &set = description.voice;
        std::array<const char *, 2> names{set.contentInput.c_str(), set.contentMask.c_str()};
        std::array<Ort::Value, 2> values{Ort::Value{nullptr}, Ort::Value{nullptr}};
        values[0] = Ort::Value::CreateTensor<float>(
            memory, const_cast<float *>(sound.data()), sound.size(), soundShape.data(), soundShape.size());
        const std::array<std::int64_t, 2> maskShape{1, static_cast<std::int64_t>(mask.size())};
        std::size_t inputs = 1;
        if (!set.contentMask.empty()) {
            values[1] = Ort::Value::CreateTensor<std::int64_t>(
                memory, mask.data(), mask.size(), maskShape.data(), maskShape.size());
            inputs = 2;
        }
        const char *const output = set.contentOutput.c_str();
        const std::vector<Ort::Value> results =
            content->Run(Ort::RunOptions{nullptr}, names.data(), values.data(), inputs, &output, 1);

        const std::vector<std::int64_t> shape = withoutOnes(results[0].GetTensorTypeAndShapeInfo().GetShape());
        const auto width = static_cast<std::size_t>(set.contentBands);
        const std::size_t wanted = windowFrames / 2;
        if (shape.size() != 2 || static_cast<std::size_t>(shape[1]) != width
            || static_cast<std::size_t>(shape[0]) < wanted) {
            return false;
        }
        const std::span<const float> given = floats(results[0], wanted * width);
        if (given.empty()) {
            return false;
        }
        // Two of the voice's frames to each of the encoder's.
        const std::size_t first = windowFrames - voiceFrames;
        for (std::size_t frame = 0; frame < voiceFrames; ++frame) {
            std::copy_n(given.begin() + static_cast<std::ptrdiff_t>((first + frame) / 2 * width),
                        width,
                        features.begin() + static_cast<std::ptrdiff_t>(frame * width));
        }
        wholeNumbers = allFinite(given);
        return true;
    }

    /// How high it is, into pitches. False as for hearContent().
    [[nodiscard]] bool hearPitch(std::span<const float> sound, bool &wholeNumbers)
    {
        const VoiceSet &set = description.voice;
        // The spectrum at its own length, spread out over the padded one:
        // the estimator takes whole blocks of frames, and what is past the
        // end stays at zero.
        spectrum.resize(bandFrames * kVoiceBands);
        analyser.analyse(sound, spectrum);
        std::ranges::fill(bands, 0.0f);
        for (std::size_t band = 0; band < kVoiceBands; ++band) {
            std::copy_n(spectrum.begin() + static_cast<std::ptrdiff_t>(band * bandFrames),
                        bandFrames,
                        bands.begin() + static_cast<std::ptrdiff_t>(band * paddedBandFrames));
        }
        const std::array<std::int64_t, 3> shape{
            1, static_cast<std::int64_t>(kVoiceBands), static_cast<std::int64_t>(paddedBandFrames)};
        Ort::Value input =
            Ort::Value::CreateTensor<float>(memory, bands.data(), bands.size(), shape.data(), shape.size());
        const char *const inputName = set.pitchInput.c_str();
        const char *const outputName = set.pitchOutput.c_str();
        const std::vector<Ort::Value> results =
            pitch->Run(Ort::RunOptions{nullptr}, &inputName, &input, 1, &outputName, 1);

        const std::vector<std::int64_t> given = withoutOnes(results[0].GetTensorTypeAndShapeInfo().GetShape());
        if (given.size() != 2 || static_cast<std::size_t>(given[1]) != kVoicePitches
            || static_cast<std::size_t>(given[0]) < windowFrames) {
            return false;
        }
        const std::span<const float> likelihoods = floats(results[0], windowFrames * kVoicePitches);
        if (likelihoods.empty()) {
            return false;
        }
        wholeNumbers = allFinite(likelihoods);
        for (std::size_t frame = 0; frame < windowFrames; ++frame) {
            pitches[frame] = pitchFromLikelihoods(likelihoods.subspan(frame * kVoicePitches, kVoicePitches));
        }
        return true;
    }

    /// The next hop of the noise: the frames before it are the same frames
    /// the last run had, so the voice makes the same of them.
    void moveNoiseOn()
    {
        const std::size_t kept = noise.size() - hopFrames * kVoiceNoiseSize;
        std::copy(noise.end() - static_cast<std::ptrdiff_t>(kept), noise.end(), noise.begin());
        std::generate(
            noise.begin() + static_cast<std::ptrdiff_t>(kept), noise.end(), [this] { return normal(random); });
    }

    /// The voice's sound for the frames it is given.
    [[nodiscard]] std::vector<Ort::Value> speak()
    {
        const VoiceSet &set = description.voice;
        const std::size_t first = windowFrames - voiceFrames;
        for (std::size_t frame = 0; frame < voiceFrames; ++frame) {
            hertz[frame] = shiftedPitch(pitches[first + frame], cents);
            steps[frame] = pitchStep(hertz[frame]);
        }
        const auto frames = static_cast<std::int64_t>(voiceFrames);
        const std::array<std::int64_t, 3> featureShape{1, frames, static_cast<std::int64_t>(set.contentBands)};
        const std::array<std::int64_t, 2> pitchShape{1, frames};
        const std::array<std::int64_t, 1> oneShape{1};
        const std::array<std::int64_t, 3> noiseShape{1, static_cast<std::int64_t>(kVoiceNoiseSize), frames};
        std::int64_t length = frames;
        std::int64_t speaker = 0;

        // The noise, frame after frame as it is kept, band after band as the
        // voice takes it.
        noiseInput.resize(noise.size());
        for (std::size_t frame = 0; frame < voiceFrames; ++frame) {
            for (std::size_t band = 0; band < kVoiceNoiseSize; ++band) {
                noiseInput[band * voiceFrames + frame] = noise[frame * kVoiceNoiseSize + band];
            }
        }

        std::vector<const char *> names{
            set.features.c_str(), set.lengths.c_str(), set.pitch.c_str(), set.pitchHz.c_str(), set.speaker.c_str()};
        std::vector<Ort::Value> values;
        values.reserve(6);
        values.push_back(Ort::Value::CreateTensor<float>(
            memory, features.data(), features.size(), featureShape.data(), featureShape.size()));
        values.push_back(Ort::Value::CreateTensor<std::int64_t>(memory, &length, 1, oneShape.data(), oneShape.size()));
        values.push_back(Ort::Value::CreateTensor<std::int64_t>(
            memory, steps.data(), steps.size(), pitchShape.data(), pitchShape.size()));
        values.push_back(
            Ort::Value::CreateTensor<float>(memory, hertz.data(), hertz.size(), pitchShape.data(), pitchShape.size()));
        values.push_back(Ort::Value::CreateTensor<std::int64_t>(memory, &speaker, 1, oneShape.data(), oneShape.size()));
        if (!set.noise.empty()) {
            names.push_back(set.noise.c_str());
            values.push_back(Ort::Value::CreateTensor<float>(
                memory, noiseInput.data(), noiseInput.size(), noiseShape.data(), noiseShape.size()));
        }
        const char *const output = description.output.c_str();
        return voice->Run(Ort::RunOptions{nullptr}, names.data(), values.data(), values.size(), &output, 1);
    }
};

VoiceConverter::VoiceConverter(const std::string &folder, const Description &description)
    : d(std::make_unique<Private>())
{
    d->description = description;
    try {
        d->open(folder);
    } catch (const std::exception &) {
        d->refusal = Refusal::RuntimeRefused;
    }
}

VoiceConverter::~VoiceConverter() = default;

Refusal VoiceConverter::refusal() const noexcept
{
    return d->refusal;
}

Device VoiceConverter::device() const noexcept
{
    return d->device;
}

void VoiceConverter::clearState()
{
    d->clear();
}

void VoiceConverter::setPitchShift(float cents)
{
    d->cents = cents;
}

bool VoiceConverter::run(std::size_t, std::span<const float> input, std::span<float> output)
{
    const Description &description = d->description;
    const VoiceSet &set = description.voice;
    const std::size_t hop = description.hopOut();
    if (input.size() != description.frameSize || output.size() != hop) {
        return false;
    }

    bool contentSound = false;
    bool pitchSound = false;
    if (!d->hearContent(input, contentSound) || !d->hearPitch(input, pitchSound)) {
        return false;
    }
    d->moveNoiseOn();
    const std::vector<Ort::Value> results = d->speak();

    // **What came back is held against what was promised**, by its size:
    // the shape differs from export to export.
    const std::size_t made = std::size_t{d->voiceFrames} * set.samplesPerFrame;
    const Ort::TensorTypeAndShapeInfo info = results[0].GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || info.GetElementCount() != made) {
        return false;
    }
    const float *const sound = results[0].GetTensorData<float>();
    const std::size_t end = made - std::size_t{set.tailFrames} * set.samplesPerFrame;
    const std::size_t crossfade = d->tail.size();
    const float *const kept = sound + (end - hop - crossfade);

    std::copy_n(kept, hop, output.begin());
    for (std::size_t index = 0; index < crossfade; ++index) {
        output[index] = d->tail[index] * (1.0f - d->fade[index]) + kept[index] * d->fade[index];
    }
    std::copy_n(kept + hop, crossfade, d->tail.begin());

    // A graph that gave what is not a number has no say in the sound, even
    // if the voice made something of it.
    if (!contentSound || !pitchSound) {
        std::ranges::fill(output, std::numeric_limits<float>::quiet_NaN());
    }
    return true;
}

} // namespace rb::modelhost
