// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb::model
{

/// **Why a folder or a model will not run.** A number, so it crosses shared
/// memory and the telemetry as one, and the interface has the words. Stable:
/// the interface repeats them in ModelState.qml, and a test holds the two
/// together.
enum class Refusal : std::uint32_t {
    None = 0,
    NoFolder = 1, ///< It is not there, or is not a folder that can be read.
    NothingToLoad = 2, ///< No file in it this version knows how to read.
    BadConfiguration = 3, ///< reverblabr-model.json cannot be read, or is out of bounds.
    FileTooLarge = 4, ///< Larger than a model file can be.
    NotAModel = 5, ///< The file is not what its name says.
    CodeFiles = 6, ///< Only files that can run code when opened.
    UnsafeReference = 7, ///< The model points at a file outside its folder.
    NotYetSupported = 8, ///< A family this version recognises and cannot run.
    WrongShape = 9, ///< The model's inputs are not what its description says.
    RuntimeRefused = 10, ///< The runtime would not load it.
    KeptCrashing = 11, ///< The host crashed three times in a minute.
    NoHost = 12, ///< The model host could not be run.
    LoadTooLong = 13, ///< It took more than two minutes to load.
    NeedsConfiguration = 14, ///< A model whose family is unknown, with no configuration.
    RateNotUsable = 15, ///< No resampler for this pair of rates.
    CycleTooLong = 16, ///< The session's cycle is longer than the spare time allows.
    NoContentModel = 17, ///< A voice with no content encoder beside it.
    NoPitchModel = 18, ///< A voice with no pitch estimator beside it.
    NoVoiceModel = 19, ///< The shared models with no voice beside them.
    ContentDoesNotFit = 20, ///< The content encoder gives another size of features than the voice takes.
};

/// The highest Refusal there is: a number from the host's memory above it is
/// no reason. Moves with the list above.
inline constexpr Refusal kLastRefusal = Refusal::ContentDoesNotFit;

/// **A model's family**: what knows how to call it (ADR-0031). A number for
/// the same reason as Refusal.
enum class Family : std::uint32_t {
    Unknown = 0,
    Configured = 1, ///< Described by reverblabr-model.json.
    DeepFilterNetStream = 2, ///< DeepFilterNet as one streaming graph.
    Cleanup = 3, ///< Another cleanup family, not built yet.
    VoiceConversion = 4, ///< A voice, a content encoder and a pitch estimator.
    Speech = 5, ///< Speech through a language model, not built yet.
};

/// What a run of the model takes that is not the sound: an input and the
/// output that feeds it next time.
struct StatePair {
    std::string input;
    std::string output;
};

/// An input that holds one number for good.
struct Constant {
    std::string input;
    float value = 0.0f;
};

/// **The three graphs of a voice conversion folder**, and what each one's
/// inputs and outputs are called: the exports do not agree on the names.
struct VoiceSet {
    /// The content encoder: sound in, what is being said out.
    std::string contentFile;
    std::string contentInput;
    /// An input that says which samples count, for the exports that have one.
    std::string contentMask;
    std::string contentOutput;
    /// The size of one frame of its features.
    std::uint32_t contentBands = 0;

    /// The pitch estimator: a spectrum in, how likely each pitch is out.
    std::string pitchFile;
    std::string pitchInput;
    std::string pitchOutput;

    /// The voice itself, in Description::modelFile, whose output is
    /// Description::output.
    std::string features;
    std::string lengths;
    std::string pitch;
    std::string pitchHz;
    std::string speaker;
    /// Empty for an export that makes its own noise.
    std::string noise;

    /// What the voice makes of one frame: samples at Description::sampleRate.
    std::uint32_t samplesPerFrame = 0;
    /// How many frames of the window the voice is given.
    std::uint32_t voiceFrames = 0;
    /// How many frames at the end of what it makes are left out: the last
    /// ones are the least sure, with nothing after them.
    std::uint32_t tailFrames = 0;
    /// How long two runs' sound overlap, at Description::sampleRate.
    std::uint32_t crossfade = 0;
};

/// **What a folder holds and how to call it.** Bounded by the time it is
/// made, whatever the folder said.
struct Description {
    Refusal refusal = Refusal::None;
    Family family = Family::Unknown;
    /// File names inside the folder, never a path.
    std::string modelFile;
    std::string licenceFile;
    /// The rate of what the model makes.
    std::uint32_t sampleRate = 0;
    /// The rate of what it takes, when that is another one: zero says the
    /// same. frameSize and hopSize are in this rate.
    std::uint32_t inputRate = 0;
    std::uint32_t channels = 0;
    std::uint32_t frameSize = 0;
    std::uint32_t hopSize = 0;
    /// What a hop makes, at sampleRate, when that differs from hopSize: zero
    /// says the same.
    std::uint32_t outputHop = 0;
    /// The model's own look-ahead, at sampleRate.
    std::uint32_t latencySamples = 0;
    /// A run so long that the block's latency has to hold it, on top of the
    /// spare time. Not part of what the host drops.
    std::uint32_t runMs = 0;
    std::uint32_t threads = 1;
    std::string input;
    std::string output;
    std::vector<StatePair> state;
    std::vector<Constant> constants;
    /// For Family::VoiceConversion.
    VoiceSet voice;

    [[nodiscard]] bool usable() const noexcept
    {
        return refusal == Refusal::None;
    }

    [[nodiscard]] std::uint32_t rateIn() const noexcept
    {
        return inputRate != 0 ? inputRate : sampleRate;
    }

    [[nodiscard]] std::uint32_t hopOut() const noexcept
    {
        return outputHop != 0 ? outputHop : hopSize;
    }
};

/// The few facts of a Description that leave the host, one "key=value" a
/// line, for the block and the panel: `reverblabr-model-host --inspect`.
[[nodiscard]] std::string describeAsText(const Description &description);
/// And back. Anything but those lines leaves the rest as a refusal: a host
/// that says something else is a host that cannot be believed.
[[nodiscard]] Description parseDescriptionText(std::string_view text);

/// **The path from a sound going in to the answer coming out**, in frames at
/// the session's rate (ADR-0031).
struct Timing {
    /// One hop.
    std::uint32_t hopFrames = 0;
    /// The model's look-ahead and the resamplers': what the host drops.
    std::uint32_t pathDelay = 0;
    /// All of it with the spare time: the block's latency.
    std::uint32_t latency = 0;
};

/// The timing of \a description at \a sessionRate, or nothing when there is
/// no resampler for the two rates. Both ends use this, so they agree.
[[nodiscard]] std::optional<Timing>
timingFor(const Description &description, std::uint32_t sessionRate, std::uint32_t spareFrames);

} // namespace rb::model
