// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/paramvalue.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// What a parameter's number means, so that the interface can label it and
/// the facade can report it. The engine never formats a number for a reader.
enum class ParamUnit : std::uint8_t {
    None,
    Decibels,
    Percent,
    Hertz,
    Milliseconds,
    Ratio,
    Samples,
    Semitones, ///< A musical interval: twelve to the octave.
    Cents, ///< Hundredths of a semitone.
};

/// How a parameter's range is spread across a slider. A frequency feels wrong
/// on a linear slider and right on a logarithmic one.
enum class ParamTaper : std::uint8_t {
    Linear,
    Logarithmic, ///< Needs a range that stays above zero.
    Decibels, ///< Linear in decibels, which is what a fader is.
};

/// The name of a unit or a taper, for the facade. Stable, never translated.
[[nodiscard]] std::string_view paramUnitName(ParamUnit unit) noexcept;
[[nodiscard]] std::string_view paramTaperName(ParamTaper taper) noexcept;

struct ParamFlags {
    /// May be changed while the session runs without the graph around the
    /// block being worked out again: the value reaches the audio thread
    /// through the parameter queue, or the block applies it to itself, and
    /// that is the whole of the change.
    ///
    /// **A parameter the session plan reads is not automatable** — which
    /// device to play to, which application to capture, whether alert
    /// sounds are wanted. Changing one of those moves ports and links,
    /// which no number sent to a block can do, so the engine plans the
    /// session again for it instead. Declaring it here is what tells
    /// the engine which is which: a list of parameter names kept anywhere
    /// else would be a second copy of what the block already knows, and the
    /// two would drift the first time a block gained a setting.
    bool automatable = true;
    /// The block writes it, nobody else: a gain reduction, an activity light.
    /// A read-only parameter is never saved and never set from outside.
    bool readOnly = false;
    /// Not shown in the generic settings panel, because the block's own face
    /// already shows it or it is for diagnostics.
    bool hidden = false;
    /// Part of what a preset holds and sets. **False for a setting that says
    /// which of a pair this block is, not how it sounds** — a hearing
    /// block's Ear. A "my left ear" preset has to load into the block
    /// for either ear, so a saved preset leaves the parameter out and
    /// applying one never changes it. The session keeps it as usual.
    bool inPresets = true;
};

/// Everything about one parameter that the DSP, the generic settings panel,
/// the presets, the session file and later the D-Bus API all need.
///
/// Ids and enum choice ids are **stable for ever**: they are what a session
/// file holds. Nothing here is translated; the interface keeps its own table
/// of names and descriptions keyed by these ids (see docs/i18n.md).
struct ParamDescriptor {
    /// Stable, for example "gainDb". Unique within the block.
    std::string id;
    ParamType type = ParamType::Float;

    /// The ends of the range, for Float and Int. Left empty for the other
    /// types, which have no range.
    ParamValue minimum;
    ParamValue maximum;
    /// What the parameter is when the block is made. Required.
    ParamValue defaultValue;

    /// For Enum: the stable ids of the choices, in the order to show them.
    std::vector<std::string> choices;

    ParamUnit unit = ParamUnit::None;
    ParamTaper taper = ParamTaper::Linear;
    /// How long a change takes to arrive in the signal, in milliseconds. Zero
    /// means it is applied at once, which only suits something that cannot
    /// click. See LinearSmoother in dsp/smoothing.h.
    float smoothingMs = 0.0f;
    ParamFlags flags;
};

/// What is wrong with a descriptor. These are programming mistakes inside our
/// own blocks, caught by a test rather than reported to the user.
enum class DescriptorError : std::uint8_t {
    EmptyId, ///< A parameter needs an id.
    DuplicateId, ///< Two parameters of one block share an id.
    MissingDefault, ///< No default value was given.
    DefaultTypeMismatch, ///< The default is not of the declared type.
    RangeTypeMismatch, ///< A range end is not of the declared type.
    RangeOnTypeWithoutOne, ///< A range was given for a type that has none.
    RangeIncomplete, ///< A numeric parameter needs both ends or neither.
    RangeInverted, ///< The minimum is above the maximum.
    DefaultOutOfRange, ///< The default is outside the range.
    IntRangeTooWide, ///< An int range beyond what the audio thread can carry.
    LogarithmicRangeNotPositive, ///< A logarithmic taper needs a range above zero.
    ChoicesOnNonEnum, ///< Choices were given for a type that has none.
    EnumWithoutChoices, ///< An enum needs at least one choice.
    EmptyChoiceId, ///< A choice needs an id.
    DuplicateChoiceId, ///< Two choices share an id.
    DefaultNotAChoice, ///< The default is not one of the choices.
    NegativeSmoothing, ///< A smoothing time cannot be negative.
    SmoothingOnUnsmoothableType, ///< Only a number can be glided to.
    ReadOnlyAndAutomatable, ///< A value the block writes cannot be automated.
};

/// The name of an error, for a test failure message and for diagnostics.
[[nodiscard]] std::string_view descriptorErrorName(DescriptorError error) noexcept;

/// The largest whole number an int parameter may use. Parameter changes reach
/// the audio thread as floats (see Block::applyParam), and beyond this they
/// would no longer be whole numbers when they arrived.
inline constexpr std::int64_t kMaxIntParamMagnitude = 1 << 24;

/// Checks one descriptor. Returns the first thing wrong with it, if anything.
[[nodiscard]] std::expected<void, DescriptorError> validateParam(const ParamDescriptor &descriptor);

/// Checks a whole block's parameter list, including that the ids differ.
/// Returns the index of the offending parameter along with the error.
struct ParamListError {
    std::size_t index = 0;
    DescriptorError error = DescriptorError::EmptyId;

    [[nodiscard]] bool operator==(const ParamListError &other) const = default;
};

[[nodiscard]] std::expected<void, ParamListError> validateParams(std::span<const ParamDescriptor> descriptors);

/// Whether \a value would be allowed for \a descriptor: the right type, in
/// range, and one of the choices for an enum.
[[nodiscard]] bool isValueAllowed(const ParamDescriptor &descriptor, const ParamValue &value);

/// \a value brought inside the descriptor's range. A value of the wrong type
/// is returned unchanged; use isValueAllowed() to tell the two apart.
[[nodiscard]] ParamValue clampToRange(const ParamDescriptor &descriptor, const ParamValue &value);

} // namespace rb::dsp
