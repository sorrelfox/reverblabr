// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/paramdescriptor.h"
#include "dsp/paramvalue.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace rb::dsp;

namespace
{

/// A descriptor that checks out, to be spoilt one thing at a time.
ParamDescriptor goodFloat()
{
    ParamDescriptor descriptor;
    descriptor.id = "gainDb";
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(-60.0f);
    descriptor.maximum = ParamValue::ofFloat(12.0f);
    descriptor.defaultValue = ParamValue::ofFloat(0.0f);
    descriptor.unit = ParamUnit::Decibels;
    descriptor.smoothingMs = 20.0f;
    return descriptor;
}

ParamDescriptor goodEnum()
{
    ParamDescriptor descriptor;
    descriptor.id = "channels";
    descriptor.type = ParamType::Enum;
    descriptor.choices = {"both", "left", "right"};
    descriptor.defaultValue = ParamValue::ofEnum("both");
    return descriptor;
}

/// Asserts that \a descriptor is rejected for \a expected, and says which
/// error actually came back when it is not.
void expectRejected(const ParamDescriptor &descriptor, DescriptorError expected)
{
    const std::expected<void, DescriptorError> result = validateParam(descriptor);
    ASSERT_FALSE(result.has_value()) << "expected " << descriptorErrorName(expected) << ", but it was accepted";
    EXPECT_EQ(result.error(), expected) << "expected " << descriptorErrorName(expected) << ", got "
                                        << descriptorErrorName(result.error());
}

} // namespace

TEST(ParamDescriptor, AGoodDescriptorIsAccepted)
{
    EXPECT_TRUE(validateParam(goodFloat()).has_value());
    EXPECT_TRUE(validateParam(goodEnum()).has_value());
}

TEST(ParamDescriptor, AParameterNeedsAnIdAndADefault)
{
    ParamDescriptor noId = goodFloat();
    noId.id.clear();
    expectRejected(noId, DescriptorError::EmptyId);

    ParamDescriptor noDefault = goodFloat();
    noDefault.defaultValue = ParamValue{};
    expectRejected(noDefault, DescriptorError::MissingDefault);
}

TEST(ParamDescriptor, TheDefaultHasToBeOfTheDeclaredType)
{
    ParamDescriptor wrong = goodFloat();
    wrong.defaultValue = ParamValue::ofInt(0);
    expectRejected(wrong, DescriptorError::DefaultTypeMismatch);
}

TEST(ParamDescriptor, ABadRangeIsCaught)
{
    ParamDescriptor inverted = goodFloat();
    inverted.minimum = ParamValue::ofFloat(12.0f);
    inverted.maximum = ParamValue::ofFloat(-60.0f);
    expectRejected(inverted, DescriptorError::RangeInverted);

    ParamDescriptor halfRange = goodFloat();
    halfRange.maximum = ParamValue{};
    expectRejected(halfRange, DescriptorError::RangeIncomplete);

    ParamDescriptor mismatched = goodFloat();
    mismatched.minimum = ParamValue::ofInt(-60);
    expectRejected(mismatched, DescriptorError::RangeTypeMismatch);

    ParamDescriptor rangeOnText = goodEnum();
    rangeOnText.minimum = ParamValue::ofFloat(0.0f);
    rangeOnText.maximum = ParamValue::ofFloat(1.0f);
    expectRejected(rangeOnText, DescriptorError::RangeOnTypeWithoutOne);
}

TEST(ParamDescriptor, ADefaultOutsideTheRangeIsCaught)
{
    ParamDescriptor tooHigh = goodFloat();
    tooHigh.defaultValue = ParamValue::ofFloat(24.0f);
    expectRejected(tooHigh, DescriptorError::DefaultOutOfRange);

    ParamDescriptor tooLow = goodFloat();
    tooLow.defaultValue = ParamValue::ofFloat(-96.0f);
    expectRejected(tooLow, DescriptorError::DefaultOutOfRange);
}

TEST(ParamDescriptor, AnIntRangeBeyondWhatAFloatCarriesIsCaught)
{
    // Parameter changes reach the audio thread as floats; past 2^24 they
    // would stop being whole numbers on the way.
    ParamDescriptor wide;
    wide.id = "samples";
    wide.type = ParamType::Int;
    wide.minimum = ParamValue::ofInt(0);
    wide.maximum = ParamValue::ofInt(kMaxIntParamMagnitude + 1);
    wide.defaultValue = ParamValue::ofInt(0);
    expectRejected(wide, DescriptorError::IntRangeTooWide);

    ParamDescriptor atTheLimit = wide;
    atTheLimit.maximum = ParamValue::ofInt(kMaxIntParamMagnitude);
    EXPECT_TRUE(validateParam(atTheLimit).has_value());
}

TEST(ParamDescriptor, ALogarithmicTaperNeedsARangeAboveZero)
{
    ParamDescriptor frequency;
    frequency.id = "frequencyHz";
    frequency.type = ParamType::Float;
    frequency.minimum = ParamValue::ofFloat(0.0f);
    frequency.maximum = ParamValue::ofFloat(20000.0f);
    frequency.defaultValue = ParamValue::ofFloat(1000.0f);
    frequency.taper = ParamTaper::Logarithmic;
    expectRejected(frequency, DescriptorError::LogarithmicRangeNotPositive);

    frequency.minimum = ParamValue::ofFloat(20.0f);
    EXPECT_TRUE(validateParam(frequency).has_value());
}

TEST(ParamDescriptor, BadChoicesAreCaught)
{
    ParamDescriptor noChoices = goodEnum();
    noChoices.choices.clear();
    expectRejected(noChoices, DescriptorError::EnumWithoutChoices);

    ParamDescriptor empty = goodEnum();
    empty.choices = {"both", ""};
    expectRejected(empty, DescriptorError::EmptyChoiceId);

    ParamDescriptor duplicate = goodEnum();
    duplicate.choices = {"both", "left", "both"};
    expectRejected(duplicate, DescriptorError::DuplicateChoiceId);

    ParamDescriptor notAChoice = goodEnum();
    notAChoice.defaultValue = ParamValue::ofEnum("middle");
    expectRejected(notAChoice, DescriptorError::DefaultNotAChoice);

    ParamDescriptor choicesOnFloat = goodFloat();
    choicesOnFloat.choices = {"one"};
    expectRejected(choicesOnFloat, DescriptorError::ChoicesOnNonEnum);
}

TEST(ParamDescriptor, BadSmoothingIsCaught)
{
    ParamDescriptor negative = goodFloat();
    negative.smoothingMs = -1.0f;
    expectRejected(negative, DescriptorError::NegativeSmoothing);

    ParamDescriptor unsmoothable = goodEnum();
    unsmoothable.smoothingMs = 20.0f;
    expectRejected(unsmoothable, DescriptorError::SmoothingOnUnsmoothableType);
}

TEST(ParamDescriptor, AValueTheBlockWritesCannotBeAutomated)
{
    ParamDescriptor output = goodFloat();
    output.flags.readOnly = true;
    expectRejected(output, DescriptorError::ReadOnlyAndAutomatable);

    output.flags.automatable = false;
    output.smoothingMs = 0.0f;
    EXPECT_TRUE(validateParam(output).has_value());
}

TEST(ParamDescriptor, ANumericParameterMayHaveNoRangeAtAll)
{
    ParamDescriptor open;
    open.id = "offset";
    open.type = ParamType::Float;
    open.defaultValue = ParamValue::ofFloat(0.0f);
    EXPECT_TRUE(validateParam(open).has_value());
}

TEST(ParamList, DuplicateIdsAreCaughtWithThePositionOfTheSecondOne)
{
    const std::array descriptors{goodFloat(), goodEnum(), goodFloat()};
    const std::expected<void, ParamListError> result = validateParams(descriptors);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), (ParamListError{2, DescriptorError::DuplicateId}));
}

TEST(ParamList, TheOffendingParameterIsNamedByPosition)
{
    ParamDescriptor broken = goodFloat();
    broken.id = "broken";
    broken.defaultValue = ParamValue::ofFloat(100.0f);
    const std::array descriptors{goodEnum(), broken};

    const std::expected<void, ParamListError> result = validateParams(descriptors);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().index, 1u);
    EXPECT_EQ(result.error().error, DescriptorError::DefaultOutOfRange);
}

TEST(ParamList, AnEmptyListIsFine)
{
    EXPECT_TRUE(validateParams({}).has_value());
}

TEST(IsValueAllowed, ChecksTypeRangeAndChoices)
{
    const ParamDescriptor gain = goodFloat();
    EXPECT_TRUE(isValueAllowed(gain, ParamValue::ofFloat(-6.0f)));
    EXPECT_TRUE(isValueAllowed(gain, ParamValue::ofFloat(-60.0f))) << "the ends of the range are inside it";
    EXPECT_TRUE(isValueAllowed(gain, ParamValue::ofFloat(12.0f)));
    EXPECT_FALSE(isValueAllowed(gain, ParamValue::ofFloat(12.1f)));
    EXPECT_FALSE(isValueAllowed(gain, ParamValue::ofInt(0))) << "an int is not a float";

    const ParamDescriptor channels = goodEnum();
    EXPECT_TRUE(isValueAllowed(channels, ParamValue::ofEnum("left")));
    EXPECT_FALSE(isValueAllowed(channels, ParamValue::ofEnum("middle")));
}

TEST(ClampToRange, BringsANumberInsideAndLeavesEverythingElseAlone)
{
    const ParamDescriptor gain = goodFloat();
    EXPECT_FLOAT_EQ(*clampToRange(gain, ParamValue::ofFloat(100.0f)).asFloat(), 12.0f);
    EXPECT_FLOAT_EQ(*clampToRange(gain, ParamValue::ofFloat(-100.0f)).asFloat(), -60.0f);
    EXPECT_FLOAT_EQ(*clampToRange(gain, ParamValue::ofFloat(-6.0f)).asFloat(), -6.0f);

    // Nothing to clamp: the wrong type and a type without a range both come
    // back untouched, so the caller can tell them apart with isValueAllowed().
    EXPECT_EQ(clampToRange(gain, ParamValue::ofEnum("left")), ParamValue::ofEnum("left"));
    EXPECT_EQ(clampToRange(goodEnum(), ParamValue::ofEnum("middle")), ParamValue::ofEnum("middle"));
}

TEST(ParamDescriptor, UnitsAndTapersHaveStableNames)
{
    EXPECT_EQ(paramUnitName(ParamUnit::Decibels), "dB");
    EXPECT_EQ(paramUnitName(ParamUnit::Hertz), "Hz");
    EXPECT_EQ(paramUnitName(ParamUnit::Milliseconds), "ms");
    EXPECT_EQ(paramTaperName(ParamTaper::Linear), "linear");
    EXPECT_EQ(paramTaperName(ParamTaper::Logarithmic), "log");
}
