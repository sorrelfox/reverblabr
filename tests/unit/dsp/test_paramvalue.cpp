// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/paramvalue.h"

#include <gtest/gtest.h>

#include <cstdint>

using namespace rb::dsp;

TEST(ParamValue, AnEmptyValueHoldsNothing)
{
    const ParamValue value;
    EXPECT_TRUE(value.isEmpty());
    EXPECT_FALSE(value.type().has_value());
    EXPECT_FALSE(value.asFloat().has_value());
    EXPECT_FALSE(value.asText().has_value());
    EXPECT_FALSE(value.asNumber().has_value());
}

TEST(ParamValue, EachTypeReadsBackAsItself)
{
    EXPECT_FLOAT_EQ(*ParamValue::ofFloat(-6.5f).asFloat(), -6.5f);
    EXPECT_EQ(*ParamValue::ofInt(-7).asInt(), -7);
    EXPECT_TRUE(*ParamValue::ofBool(true).asBool());
    // **Named, so that the view has something to look into.** The views are
    // deleted on a temporary; this test wants the type-specific accessor
    // rather than text(), which answers for all three alike.
    const ParamValue choice = ParamValue::ofEnum("left");
    const ParamValue pattern = ParamValue::ofString("podcast-%d");
    const ParamValue device = ParamValue::ofDeviceRef("alsa_output.pci-0000_00_1f.3");
    EXPECT_EQ(*choice.asEnum(), "left");
    EXPECT_EQ(*pattern.asString(), "podcast-%d");
    EXPECT_EQ(*device.asDeviceRef(), "alsa_output.pci-0000_00_1f.3");
}

TEST(ParamValue, ThereIsNoSilentConversionBetweenTypes)
{
    // A parameter declared as an int must not be readable as a float by
    // accident: the session file and the facade both depend on the type.
    const ParamValue integer = ParamValue::ofInt(3);
    EXPECT_FALSE(integer.asFloat().has_value());
    EXPECT_FALSE(integer.asBool().has_value());
    EXPECT_FALSE(integer.asString().has_value());

    const ParamValue text = ParamValue::ofString("3");
    EXPECT_FALSE(text.asNumber().has_value());
    EXPECT_FALSE(text.asEnum().has_value()) << "a string is not an enum, however alike they look";
    EXPECT_FALSE(text.asDeviceRef().has_value());
}

TEST(ParamValue, TheNumberOfAnythingNumericIsReadable)
{
    EXPECT_DOUBLE_EQ(*ParamValue::ofFloat(2.5f).asNumber(), 2.5);
    EXPECT_DOUBLE_EQ(*ParamValue::ofInt(9).asNumber(), 9.0);
    EXPECT_DOUBLE_EQ(*ParamValue::ofBool(true).asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(*ParamValue::ofBool(false).asNumber(), 0.0);
    EXPECT_FALSE(ParamValue::ofEnum("left").asNumber().has_value());
}

TEST(ParamValue, TheTextOfAnythingStringShapedIsReadable)
{
    EXPECT_EQ(ParamValue::ofEnum("left").text(), "left");
    EXPECT_EQ(ParamValue::ofString("hello").text(), "hello");
    EXPECT_EQ(ParamValue::ofDeviceRef("default").text(), "default");
    // **Empty text and no text are not the same answer**, and text() gives
    // "" for both, so the absence is asked of the view.
    const ParamValue number = ParamValue::ofFloat(1.0f);
    EXPECT_FALSE(number.asText().has_value());
    EXPECT_TRUE(number.text().empty());
}

TEST(ParamValue, ValuesOfDifferentTypesAreNeverEqual)
{
    EXPECT_EQ(ParamValue::ofFloat(1.0f), ParamValue::ofFloat(1.0f));
    EXPECT_NE(ParamValue::ofFloat(1.0f), ParamValue::ofInt(1));
    EXPECT_NE(ParamValue::ofBool(true), ParamValue::ofInt(1));
    EXPECT_NE(ParamValue::ofEnum("left"), ParamValue::ofString("left"));
    EXPECT_NE(ParamValue::ofFloat(1.0f), ParamValue{});
    EXPECT_EQ(ParamValue{}, ParamValue{});
}

TEST(ParamValue, TypesHaveStableNames)
{
    // These names travel over the facade, so they are part of the interface.
    EXPECT_EQ(paramTypeName(ParamType::Float), "float");
    EXPECT_EQ(paramTypeName(ParamType::Int), "int");
    EXPECT_EQ(paramTypeName(ParamType::Bool), "bool");
    EXPECT_EQ(paramTypeName(ParamType::Enum), "enum");
    EXPECT_EQ(paramTypeName(ParamType::String), "string");
    EXPECT_EQ(paramTypeName(ParamType::DeviceRef), "deviceRef");
    EXPECT_EQ(paramTypeName(ParamType::StringList), "stringList");
}

TEST(ParamValue, HoldsSeveralStringsInTheOrderTheyWereGiven)
{
    // The sinks an application's audio is taken from, the applications a
    // capture leaves out: a list, and not a joined string, because a name
    // with a comma in it is a name.
    const ParamValue value = ParamValue::ofStringList({"alsa_output.analog", "bluez.headset"});
    ASSERT_TRUE(value.is(ParamType::StringList));
    const std::optional<std::vector<std::string>> read = value.asStringList();
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->size(), 2U);
    EXPECT_EQ(read->at(0), "alsa_output.analog");
    EXPECT_EQ(read->at(1), "bluez.headset");

    // It is none of the other types, and none of them is a list.
    EXPECT_FALSE(value.asText().has_value());
    EXPECT_FALSE(value.asNumber().has_value());
    EXPECT_FALSE(ParamValue::ofString("one").asStringList().has_value());
}

TEST(ParamValue, AListOfNothingIsAValueAndNotAnAbsence)
{
    // "These outputs" with none chosen means nothing is captured, which is
    // not the same as nobody having said.
    const ParamValue empty = ParamValue::ofStringList({});
    EXPECT_FALSE(empty.isEmpty());
    ASSERT_TRUE(empty.asStringList().has_value());
    EXPECT_TRUE(empty.asStringList()->empty());
    EXPECT_NE(empty, ParamValue{});
}

TEST(ParamValueMap, IsOrderedByIdSoTwoEqualStatesAreWrittenAlike)
{
    ParamValueMap first;
    first.emplace("zebra", ParamValue::ofInt(1));
    first.emplace("alpha", ParamValue::ofInt(2));

    ParamValueMap second;
    second.emplace("alpha", ParamValue::ofInt(2));
    second.emplace("zebra", ParamValue::ofInt(1));

    EXPECT_EQ(first, second);
    EXPECT_EQ(first.begin()->first, "alpha");
}

TEST(ParamValueMap, LooksUpByStringViewWithoutBuildingAString)
{
    ParamValueMap state;
    state.emplace("gainDb", ParamValue::ofFloat(-3.0f));
    const std::string_view id = "gainDb";
    ASSERT_NE(state.find(id), state.end());
    EXPECT_FLOAT_EQ(*state.find(id)->second.asFloat(), -3.0f);
}
