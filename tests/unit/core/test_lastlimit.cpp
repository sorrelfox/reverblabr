// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Whether an Output Device needs a limit of its own, and at what ceiling,
// read from a session's wires.

#include "blocks/builtinblocks.h"
#include "blocks/outputdevice.h"
#include "core/lastlimit.h"
#include "core/session.h"
#include "dsp/blockregistry.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <gtest/gtest.h>

#include <utility>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

constexpr auto kSource = "source.application";
constexpr auto kLift = "hearing.compensation16";
constexpr auto kLowering = "hearing.frequencylowering";
constexpr auto kEq = "effect.equalizer10";
constexpr auto kLimiterBlock = "effect.limiter";
constexpr auto kVolume = "effect.volume";
constexpr auto kMeter = "meter.peakrms";
constexpr auto kJoin = "hearing.joinlr";
constexpr auto kSwitch = "advanced.inputswitch";
constexpr auto kMixer = "advanced.mixer";
constexpr auto kOutput = "output.device";

class LastLimit : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
        facts = limitFactsOf(registry);
    }

    QString add(const char *type, QJsonObject params = {})
    {
        BlockInstance block;
        block.id = u"b%1"_s.arg(session.blocks.size() + 1);
        block.type = QString::fromLatin1(type);
        block.params = std::move(params);
        session.blocks.append(block);
        return block.id;
    }

    /// Both sides of one block's output into another's first lane.
    void wire(const QString &from, const QString &to, std::uint32_t toFirst = 0)
    {
        session.wires.append(Wire{from, 0, to, toFirst});
        session.wires.append(Wire{from, 1, to, toFirst + 1});
    }

    /// Blocks in a row, from a source to an Output Device, which it returns.
    QString chain(const QList<QString> &blocks)
    {
        const QString output = add(kOutput);
        QString previous = add(kSource);
        for (const QString &block : blocks) {
            wire(previous, block);
            previous = block;
        }
        wire(previous, output);
        return output;
    }

    [[nodiscard]] const BlockInstance &block(const QString &id) const
    {
        return *session.findBlock(id);
    }

    [[nodiscard]] bool needed(const QString &output) const
    {
        return lastLimitNeeded(session, block(output), facts);
    }

    [[nodiscard]] float applied(const QString &output) const
    {
        return appliedLimitDb(session, block(output), facts);
    }

    dsp::BlockRegistry registry;
    LimitFacts facts;
    Session session;
};

} // namespace

TEST_F(LastLimit, ASessionWithNoBlockOfTheGroupNeverGetsOne)
{
    EXPECT_FALSE(needed(chain({add(kEq), add(kVolume)})));
}

TEST_F(LastLimit, AChainEndingInSoftLiftPaysNothing)
{
    EXPECT_FALSE(needed(chain({add(kLift)})));
}

TEST_F(LastLimit, FrequencyLoweringThenSoftLiftPaysNothing)
{
    EXPECT_FALSE(needed(chain({add(kLowering), add(kLift)})));
}

TEST_F(LastLimit, FrequencyLoweringAloneNeedsOne)
{
    const QString lowering = add(kLowering);
    const QString output = chain({lowering});
    EXPECT_TRUE(needed(output));
    const LimitReason reason = lastLimitReason(session, block(output), facts);
    EXPECT_EQ(reason.cause, LimitCause::Block);
    EXPECT_EQ(reason.blockId, lowering);
}

TEST_F(LastLimit, SoftLiftThenFrequencyLoweringNeedsOne)
{
    EXPECT_TRUE(needed(chain({add(kLift), add(kLowering)})));
}

TEST_F(LastLimit, SoftLiftThenVolumeNeedsOne)
{
    EXPECT_TRUE(needed(chain({add(kLift), add(kVolume)})));
}

TEST_F(LastLimit, SoftLiftThenAnEqualiserNeedsOne)
{
    EXPECT_TRUE(needed(chain({add(kLift), add(kEq)})));
}

TEST_F(LastLimit, AMeterAfterSoftLiftChangesNothing)
{
    EXPECT_FALSE(needed(chain({add(kLift), add(kMeter)})));
}

TEST_F(LastLimit, TwoEarsJoinedPayNothingAndAnInputSwitchAfterThemDoes)
{
    const QString source = add(kSource);
    const QString left = add(kLift, {{u"ear"_s, u"left"_s}});
    const QString right = add(kLift, {{u"ear"_s, u"right"_s}});
    const QString join = add(kJoin);
    const QString output = add(kOutput);
    wire(source, left);
    wire(source, right);
    wire(left, join, 0);
    wire(right, join, 2);
    wire(join, output);
    EXPECT_FALSE(needed(output));

    // The Join takes only the left lane's left and the right lane's right.
    session.wires.removeIf([&](const Wire &wire) { return wire.toBlock == output; });
    const QString fade = add(kSwitch);
    wire(join, fade);
    wire(fade, output);
    EXPECT_TRUE(needed(output));
}

TEST_F(LastLimit, TwoWiresIntoOneInputAreASumNothingLimits)
{
    const QString source = add(kSource);
    const QString lift = add(kLift);
    const QString output = add(kOutput);
    wire(source, lift);
    wire(lift, output);
    EXPECT_FALSE(needed(output));
    wire(source, output);
    EXPECT_TRUE(needed(output));
    EXPECT_EQ(lastLimitReason(session, block(output), facts).cause, LimitCause::TwoWires);
}

TEST_F(LastLimit, LiftOnOneEarNeedsNoLimitForTheOther)
{
    EXPECT_FALSE(needed(chain({add(kLift, {{u"ear"_s, u"left"_s}})})));
}

TEST_F(LastLimit, LoweringOnOneEarWithLiftOnBothIsReadSideBySide)
{
    const QString lowering = add(kLowering, {{u"ear"_s, u"left"_s}});
    const QString left = add(kLift, {{u"ear"_s, u"left"_s}});
    const QString right = add(kLift, {{u"ear"_s, u"right"_s}});
    EXPECT_FALSE(needed(chain({lowering, left, right})));
}

TEST_F(LastLimit, TheOtherEarOfASoftLiftStillSeesWhatStandsBeforeIt)
{
    // Soft Lift on the left does nothing for the right, where the equaliser
    // can still add.
    EXPECT_TRUE(needed(chain({add(kEq), add(kLift, {{u"ear"_s, u"left"_s}})})));
}

TEST_F(LastLimit, ABlockSwitchedOffPassesWhatComesIn)
{
    const QString lift = add(kLift);
    const QString other = add(kLift, {{u"ear"_s, u"right"_s}});
    const QString output = chain({add(kEq), lift, other});
    EXPECT_FALSE(needed(output));
    session.findBlock(lift)->enabled = false;
    EXPECT_TRUE(needed(output)) << "the equaliser now stands after the last ceiling";

    Session plain;
    std::swap(plain, session);
    const QString off = add(kLift);
    const QString second = chain({off});
    session.findBlock(off)->enabled = false;
    EXPECT_FALSE(needed(second)) << "nothing is left to add gain";
}

TEST_F(LastLimit, AJoinSwitchedOffSumsItsLanes)
{
    // Bypassed, it folds every lane into the first, so each side carries both
    // ears' lanes: two limited signals, and nothing limits their sum.
    const QString source = add(kSource);
    const QString left = add(kLift, {{u"ear"_s, u"left"_s}});
    const QString right = add(kLift, {{u"ear"_s, u"right"_s}});
    const QString join = add(kJoin);
    const QString output = add(kOutput);
    wire(source, left);
    wire(source, right);
    wire(left, join, 0);
    wire(right, join, 2);
    wire(join, output);
    EXPECT_FALSE(needed(output));
    session.findBlock(join)->enabled = false;
    const LimitReason reason = lastLimitReason(session, block(output), facts);
    EXPECT_EQ(reason.cause, LimitCause::TwoWires);
    EXPECT_EQ(reason.blockId, join);
}

TEST_F(LastLimit, AMixerSwitchedOffSumsItsLanes)
{
    const QString source = add(kSource);
    const QString first = add(kLift);
    const QString second = add(kLift);
    const QString mixer = add(kMixer);
    const QString output = add(kOutput);
    wire(source, first);
    wire(source, second);
    wire(first, mixer, 0);
    wire(second, mixer, 2);
    wire(mixer, output);
    session.findBlock(mixer)->enabled = false;
    EXPECT_EQ(lastLimitReason(session, block(output), facts).cause, LimitCause::TwoWires);
}

TEST_F(LastLimit, ABlockSwitchedOffPassesALaterLaneToo)
{
    const QString eq = add(kEq);
    const QString mixer = add(kMixer);
    const QString lift = add(kLift);
    const QString output = add(kOutput);
    const QString source = add(kSource);
    wire(source, lift);
    wire(lift, eq);
    wire(eq, mixer, 2);
    wire(mixer, output);
    session.findBlock(mixer)->enabled = false;
    const LimitReason reason = lastLimitReason(session, block(output), facts);
    EXPECT_EQ(reason.cause, LimitCause::Block);
    EXPECT_EQ(reason.blockId, eq);
}

TEST_F(LastLimit, ABlockWiredOnlyOnALaterLaneIsNoSource)
{
    const QString fade = add(kSwitch);
    const QString output = add(kOutput);
    const QString source = add(kSource);
    const QString lift = add(kLift);
    const QString eq = add(kEq);
    wire(source, lift);
    wire(lift, eq);
    wire(eq, fade, 2);
    wire(fade, output);
    const LimitReason reason = lastLimitReason(session, block(output), facts);
    EXPECT_EQ(reason.cause, LimitCause::Block);
    EXPECT_EQ(reason.blockId, fade);
}

TEST_F(LastLimit, AJoinWithOneLaneWiredStopsOnItsEmptyPort)
{
    const QString source = add(kSource);
    const QString lift = add(kLift);
    const QString join = add(kJoin);
    const QString output = add(kOutput);
    wire(source, lift);
    wire(lift, join, 0);
    wire(join, output);
    EXPECT_FALSE(needed(output));
}

TEST_F(LastLimit, ALimiterBlockIsNoLastLimit)
{
    const QString output = chain({add(kLift, {{u"limiterCeilingDb"_s, -12.0}}), add(kEq), add(kLimiterBlock)});
    EXPECT_TRUE(needed(output));
    EXPECT_FLOAT_EQ(applied(output), -12.0f);
}

TEST_F(LastLimit, BothSidesOnOneChannelNeedOne)
{
    const QString output = chain({add(kLift)});
    EXPECT_FALSE(needed(output));
    session.findBlock(output)->params = {{u"leftChannel"_s, u"FL"_s}, {u"rightChannel"_s, u"FL"_s}};
    EXPECT_TRUE(needed(output));
    EXPECT_EQ(lastLimitReason(session, block(output), facts).cause, LimitCause::OneChannel);

    session.findBlock(output)->params = {{u"leftChannel"_s, u"FL"_s}, {u"rightChannel"_s, u"FR"_s}};
    EXPECT_FALSE(needed(output));
}

TEST_F(LastLimit, TheLowestCeilingUpstreamBinds)
{
    const QString left = add(kLift, {{u"ear"_s, u"left"_s}, {u"limiterCeilingDb"_s, -6.0}});
    const QString right = add(kLift, {{u"ear"_s, u"right"_s}, {u"limiterCeilingDb"_s, -12.0}});
    const QString output = chain({left, right});
    EXPECT_FLOAT_EQ(applied(output), -12.0f);
    session.findBlock(output)->params = {{u"limitDb"_s, -20.0}};
    EXPECT_FLOAT_EQ(applied(output), -20.0f);
}

TEST_F(LastLimit, ACeilingNobodySetIsTheBlocksOwnDefault)
{
    EXPECT_FLOAT_EQ(applied(chain({add(kLift)})), blocks::OutputDevice::kLimitHighestDb);
}

TEST_F(LastLimit, ALoopInASessionThatCannotRunDoesNotHangTheWalk)
{
    const QString lift = add(kLift);
    const QString meter = add(kMeter);
    const QString output = add(kOutput);
    wire(meter, lift);
    wire(lift, meter);
    session.wires.append(Wire{meter, 0, output, 0});
    EXPECT_FALSE(needed(output));
}
