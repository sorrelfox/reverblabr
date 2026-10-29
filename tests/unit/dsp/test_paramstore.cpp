// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/block.h"
#include "dsp/paramdescriptor.h"
#include "dsp/paramstore.h"
#include "dsp/paramvalue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace rb::dsp;

namespace
{

enum Index : std::uint32_t {
    Gain = 0,
    Count,
    Muted,
    Channels,
    Pattern,
    Device,
    Reduction
};

/// One parameter of every type, plus a read-only output.
std::vector<ParamDescriptor> everyType()
{
    std::vector<ParamDescriptor> descriptors(7);

    descriptors[Gain].id = "gainDb";
    descriptors[Gain].type = ParamType::Float;
    descriptors[Gain].minimum = ParamValue::ofFloat(-60.0f);
    descriptors[Gain].maximum = ParamValue::ofFloat(12.0f);
    descriptors[Gain].defaultValue = ParamValue::ofFloat(0.0f);
    descriptors[Gain].smoothingMs = 20.0f;

    descriptors[Count].id = "bands";
    descriptors[Count].type = ParamType::Int;
    descriptors[Count].minimum = ParamValue::ofInt(1);
    descriptors[Count].maximum = ParamValue::ofInt(10);
    descriptors[Count].defaultValue = ParamValue::ofInt(3);

    descriptors[Muted].id = "muted";
    descriptors[Muted].type = ParamType::Bool;
    descriptors[Muted].defaultValue = ParamValue::ofBool(false);

    descriptors[Channels].id = "channels";
    descriptors[Channels].type = ParamType::Enum;
    descriptors[Channels].choices = {"both", "left", "right"};
    descriptors[Channels].defaultValue = ParamValue::ofEnum("both");

    descriptors[Pattern].id = "pattern";
    descriptors[Pattern].type = ParamType::String;
    descriptors[Pattern].defaultValue = ParamValue::ofString("recording");

    descriptors[Device].id = "device";
    descriptors[Device].type = ParamType::DeviceRef;
    descriptors[Device].defaultValue = ParamValue::ofDeviceRef("default");

    descriptors[Reduction].id = "gainReductionDb";
    descriptors[Reduction].type = ParamType::Float;
    descriptors[Reduction].minimum = ParamValue::ofFloat(-60.0f);
    descriptors[Reduction].maximum = ParamValue::ofFloat(0.0f);
    descriptors[Reduction].defaultValue = ParamValue::ofFloat(0.0f);
    descriptors[Reduction].flags.readOnly = true;
    descriptors[Reduction].flags.automatable = false;

    return descriptors;
}

} // namespace

TEST(ParamStore, ItsOwnDescriptorsCheckOut)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    EXPECT_TRUE(validateParams(descriptors).has_value());
}

TEST(ParamStore, StartsAtTheDefaults)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    const ParamStore store(descriptors);

    EXPECT_EQ(store.size(), 7u);
    EXPECT_EQ(store.value("gainDb"), ParamValue::ofFloat(0.0f));
    EXPECT_EQ(store.value("bands"), ParamValue::ofInt(3));
    EXPECT_EQ(store.value("muted"), ParamValue::ofBool(false));
    EXPECT_EQ(store.value("channels"), ParamValue::ofEnum("both"));
    EXPECT_EQ(store.value("pattern"), ParamValue::ofString("recording"));
    EXPECT_EQ(store.value("device"), ParamValue::ofDeviceRef("default"));
}

TEST(ParamStore, FindsParametersByIdAndSaysWhenThereIsNoSuchOne)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    const ParamStore store(descriptors);

    EXPECT_EQ(store.indexOf("channels"), Channels);
    EXPECT_FALSE(store.indexOf("nothing").has_value());
    EXPECT_TRUE(store.value("nothing").isEmpty());
    EXPECT_TRUE(store.value(99u).isEmpty());
}

TEST(ParamStore, SettingChecksTheTypeAndTheRange)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);

    EXPECT_TRUE(store.set("gainDb", ParamValue::ofFloat(-6.0f)).has_value());
    EXPECT_FLOAT_EQ(*store.value("gainDb").asFloat(), -6.0f);

    EXPECT_EQ(store.set("gainDb", ParamValue::ofInt(-6)).error(), BlockError::ParamTypeMismatch);
    EXPECT_EQ(store.set("gainDb", ParamValue::ofFloat(100.0f)).error(), BlockError::ParamOutOfRange);
    EXPECT_EQ(store.set("channels", ParamValue::ofEnum("middle")).error(), BlockError::ParamOutOfRange);
    EXPECT_EQ(store.set("nothing", ParamValue::ofFloat(0.0f)).error(), BlockError::UnknownParam);
    EXPECT_FLOAT_EQ(*store.value("gainDb").asFloat(), -6.0f) << "a refused value changes nothing";
}

TEST(ParamStore, NobodyOutsideTheBlockSetsAReadOnlyParameter)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);

    EXPECT_EQ(store.set("gainReductionDb", ParamValue::ofFloat(-3.0f)).error(), BlockError::ParamReadOnly);
    // The block itself publishes it, which is what setReadOnly() is for.
    EXPECT_TRUE(store.setReadOnly(Reduction, ParamValue::ofFloat(-3.0f)).has_value());
    EXPECT_FLOAT_EQ(*store.value("gainReductionDb").asFloat(), -3.0f);
}

TEST(ParamStore, TheRealTimePathWritesNumbersAndClampsThem)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);

    store.applyNumber(Gain, -12.0f);
    EXPECT_FLOAT_EQ(store.number(Gain), -12.0f);
    store.applyNumber(Gain, 1000.0f);
    EXPECT_FLOAT_EQ(store.number(Gain), 12.0f) << "a value from the queue is clamped, not refused";

    store.applyNumber(Count, 4.4f);
    EXPECT_FLOAT_EQ(store.number(Count), 4.0f) << "an int stays a whole number";
    EXPECT_EQ(store.value("bands"), ParamValue::ofInt(4));

    store.applyNumber(Muted, 1.0f);
    EXPECT_EQ(store.value("muted"), ParamValue::ofBool(true));

    store.applyNumber(Channels, 2.0f);
    EXPECT_EQ(store.value("channels"), ParamValue::ofEnum("right"));
    store.applyNumber(Channels, 99.0f);
    EXPECT_EQ(store.value("channels"), ParamValue::ofEnum("right")) << "clamped to the last choice";
}

TEST(ParamStore, TheRealTimePathLeavesTextAlone)
{
    // Text owns memory, so setting it on the audio thread would free the old
    // string there. The store refuses rather than doing it.
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);

    store.applyNumber(Pattern, 5.0f);
    store.applyNumber(Device, 5.0f);
    EXPECT_EQ(store.value("pattern"), ParamValue::ofString("recording"));
    EXPECT_EQ(store.value("device"), ParamValue::ofDeviceRef("default"));
}

TEST(ParamStore, AnIndexOutOfRangeIsIgnoredOnTheRealTimePath)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);
    store.applyNumber(99, 1.0f); // must not write past the end
    EXPECT_FLOAT_EQ(store.number(99), 0.0f);
}

TEST(ParamStore, StateRoundTripsExactlyForEveryType)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore written(descriptors);
    ASSERT_TRUE(written.set("gainDb", ParamValue::ofFloat(-6.5f)).has_value());
    ASSERT_TRUE(written.set("bands", ParamValue::ofInt(7)).has_value());
    ASSERT_TRUE(written.set("muted", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(written.set("channels", ParamValue::ofEnum("right")).has_value());
    ASSERT_TRUE(written.set("pattern", ParamValue::ofString("podcast-%n")).has_value());
    ASSERT_TRUE(written.set("device", ParamValue::ofDeviceRef("alsa_output.usb-Focusrite")).has_value());

    const ParamValueMap state = written.save();
    ParamStore read(descriptors);
    ASSERT_TRUE(read.load(state).has_value());

    EXPECT_EQ(read.save(), state);
    for (const auto &[id, value] : state) {
        EXPECT_EQ(read.value(id), value) << "parameter " << id;
    }
}

TEST(ParamStore, ABlockOutputIsNotState)
{
    // Read-only parameters are telemetry: they are published, not saved, and
    // loading a state never puts one back.
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);
    ASSERT_TRUE(store.setReadOnly(Reduction, ParamValue::ofFloat(-9.0f)).has_value());

    const ParamValueMap state = store.save();
    EXPECT_FALSE(state.contains("gainReductionDb"));
    EXPECT_EQ(state.size(), 6u);

    ParamValueMap withOutput = state;
    withOutput.emplace("gainReductionDb", ParamValue::ofFloat(-30.0f));
    ParamStore other(descriptors);
    ASSERT_TRUE(other.load(withOutput).has_value());
    EXPECT_FLOAT_EQ(*other.value("gainReductionDb").asFloat(), 0.0f) << "it stayed at its default";
}

TEST(ParamStore, LoadingSetsWhatIsMissingBackToItsDefault)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);
    ASSERT_TRUE(store.set("gainDb", ParamValue::ofFloat(-20.0f)).has_value());
    ASSERT_TRUE(store.set("muted", ParamValue::ofBool(true)).has_value());

    ParamValueMap partial;
    partial.emplace("gainDb", ParamValue::ofFloat(-3.0f));
    ASSERT_TRUE(store.load(partial).has_value());

    EXPECT_FLOAT_EQ(*store.value("gainDb").asFloat(), -3.0f);
    EXPECT_FALSE(*store.value("muted").asBool()) << "what the state left out went back to its default";
}

TEST(ParamStore, AnUnknownIdInASavedStateIsIgnored)
{
    // A session written by a later version mentions parameters this one does
    // not have. It still has to open.
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);

    ParamValueMap fromTheFuture;
    fromTheFuture.emplace("gainDb", ParamValue::ofFloat(-3.0f));
    fromTheFuture.emplace("sidechainSource", ParamValue::ofString("b7"));
    ASSERT_TRUE(store.load(fromTheFuture).has_value());
    EXPECT_FLOAT_EQ(*store.value("gainDb").asFloat(), -3.0f);
}

TEST(ParamStore, AKnownIdWithABadValueIsAnError)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);

    ParamValueMap corrupt;
    corrupt.emplace("channels", ParamValue::ofEnum("upside-down"));
    EXPECT_EQ(store.load(corrupt).error(), BlockError::ParamOutOfRange);

    ParamValueMap wrongType;
    wrongType.emplace("gainDb", ParamValue::ofString("-3"));
    EXPECT_EQ(store.load(wrongType).error(), BlockError::ParamTypeMismatch);
}

TEST(ParamStore, AnEnumIsSavedAsItsIdSoReorderingTheChoicesIsSafe)
{
    // The whole point of saving the id rather than the position: a later
    // version that puts the choices in another order still reads old files
    // correctly.
    std::vector<ParamDescriptor> original = everyType();
    ParamStore store(original);
    ASSERT_TRUE(store.set("channels", ParamValue::ofEnum("right")).has_value());
    const ParamValueMap state = store.save();
    EXPECT_EQ(*state.at("channels").asEnum(), "right");

    std::vector<ParamDescriptor> reordered = everyType();
    reordered[Channels].choices = {"right", "left", "both"};
    ParamStore later(reordered);
    ASSERT_TRUE(later.load(state).has_value());
    EXPECT_EQ(later.value("channels"), ParamValue::ofEnum("right"));
    EXPECT_FLOAT_EQ(later.number(Channels), 0.0f) << "the same choice, at its new position";
}

TEST(ParamStore, ResettingPutsEveryDefaultBack)
{
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);
    ASSERT_TRUE(store.set("gainDb", ParamValue::ofFloat(-20.0f)).has_value());
    ASSERT_TRUE(store.set("pattern", ParamValue::ofString("changed")).has_value());

    store.resetToDefaults();
    EXPECT_EQ(store.save(), ParamStore(descriptors).save());
}

TEST(ParamStore, AnEmptyParameterListWorks)
{
    const ParamStore store({});
    EXPECT_EQ(store.size(), 0u);
    EXPECT_TRUE(store.save().empty());
}

TEST(ParamStore, KeepsAListAndGivesItBackForSaving)
{
    // A list that a store took but did not keep would be a parameter the
    // next save quietly dropped, and a session that forgot what it was told.
    ParamDescriptor outputs;
    outputs.id = "outputs";
    outputs.type = ParamType::StringList;
    outputs.defaultValue = ParamValue::ofStringList({"alsa_output.analog"});
    const std::vector<ParamDescriptor> descriptors{outputs};

    ParamStore store(descriptors);
    EXPECT_EQ(store.value("outputs").asStringList()->size(), 1U);

    ASSERT_TRUE(store.set("outputs", ParamValue::ofStringList({"one", "two", "three"})).has_value());
    ASSERT_TRUE(store.value("outputs").asStringList().has_value());
    EXPECT_EQ(store.value("outputs").asStringList()->size(), 3U);
    EXPECT_EQ(store.value("outputs").asStringList()->at(2), "three");

    const ParamValueMap saved = store.save();
    ASSERT_TRUE(saved.contains("outputs"));
    EXPECT_EQ(saved.at("outputs").asStringList()->size(), 3U);

    // And it comes back into another store of the same shape.
    ParamStore opened(descriptors);
    ASSERT_TRUE(opened.load(saved).has_value());
    EXPECT_EQ(opened.value("outputs").asStringList()->at(0), "one");

    // A state that says nothing about it puts the default back, as for any
    // other parameter.
    ASSERT_TRUE(opened.load({}).has_value());
    EXPECT_EQ(opened.value("outputs").asStringList()->at(0), "alsa_output.analog");

    // The wrong type is refused rather than taken as something else.
    EXPECT_FALSE(store.set("outputs", ParamValue::ofString("one")).has_value());
    // And the audio thread cannot set it: text owns memory.
    store.applyNumber(0, 2.0f);
    EXPECT_EQ(store.value("outputs").asStringList()->size(), 3U);
}

TEST(ParamStoreTest, SavingWhileTheAudioThreadChangesNumbersReadsWholeValues)
{
    // The audio thread sets numbers with applyNumber() while the
    // engine thread saves, as autosave does while a session runs. Every
    // value a save reads is one the audio thread wrote, whole, and never a
    // mixture of two; and under tsan the sharing itself is clean.
    //
    // **Until the saver has saved a few hundred times**, not for a fixed
    // number of rounds: on a loaded machine the saving thread was once not
    // scheduled at all before twenty thousand rounds were over, and
    // the test proved nothing. Bounded at thirty seconds.
    constexpr int kSaves = 200;
    constexpr int kLeastRounds = 20000;
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);
    std::atomic<bool> done{false};
    std::atomic<int> saves{0};
    std::atomic<int> strange{0};
    std::thread saver([&] {
        while (!done.load()) {
            const ParamValueMap state = store.save();
            const float gain = *state.at("gainDb").asFloat();
            // Only the two values the audio thread ever writes.
            if (gain != -6.0f && gain != 6.0f && gain != 0.0f) {
                ++strange;
            }
            const std::string channels = state.at("channels").text();
            if (channels != "both" && channels != "left" && channels != "right") {
                ++strange;
            }
            ++saves;
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    int round = 0;
    for (; round < kLeastRounds || saves.load() < kSaves; ++round) {
        store.applyNumber(Gain, round % 2 == 0 ? -6.0f : 6.0f);
        store.applyNumber(Channels, static_cast<float>(round % 3));
        store.applyNumber(Muted, static_cast<float>(round % 2));
        if (round % 1024 == 0 && std::chrono::steady_clock::now() > deadline) {
            break;
        }
    }
    done = true;
    saver.join();
    EXPECT_EQ(strange.load(), 0);
    EXPECT_GE(saves.load(), kSaves) << "the saver ran " << saves.load() << " times in " << round << " rounds";
}

TEST(ParamStoreTest, TheAudioThreadReadsWhatAnotherThreadSets)
{
    // The other direction: the engine thread sets a number through set()
    // while the audio thread reads it with number().
    const std::vector<ParamDescriptor> descriptors = everyType();
    ParamStore store(descriptors);
    std::atomic<bool> done{false};
    std::atomic<int> strange{0};
    std::thread audio([&] {
        while (!done.load()) {
            const float gain = store.number(Gain);
            if (gain != -3.0f && gain != 3.0f && gain != 0.0f) {
                ++strange;
            }
        }
    });
    for (int round = 0; round < 20000; ++round) {
        ASSERT_TRUE(store.set(Gain, ParamValue::ofFloat(round % 2 == 0 ? -3.0f : 3.0f)).has_value());
    }
    done = true;
    audio.join();
    EXPECT_EQ(strange.load(), 0);
}
