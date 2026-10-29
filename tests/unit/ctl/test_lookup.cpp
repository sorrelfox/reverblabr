// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// reverblabr-ctl finding what a person typed, and reading a value for a
// setting, without a bus.

#include "lookup.h"

#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>

using namespace rb::ctl;
using namespace Qt::StringLiterals;

namespace
{

const std::vector<SessionEntry> kSessions{
    {u"11111111-aaaa"_s, u"Evening radio"_s, u"running"_s},
    {u"22222222-bbbb"_s, u"Podcast — Mic + Call ☕"_s, u"closed"_s},
    {u"33333333-cccc"_s, u"Twin"_s, u"idle"_s},
    {u"44444444-dddd"_s, u"Twin"_s, u"closed"_s},
    {u"55555555-eeee"_s, u"twin"_s, u"closed"_s},
};

} // namespace

TEST(Lookup, ASessionIsFoundByItsIdOrItsNameUnicodeAndSpacesIncluded)
{
    EXPECT_EQ(findSession(kSessions, u"11111111-aaaa"_s)->name, u"Evening radio"_s);
    EXPECT_EQ(findSession(kSessions, u"Podcast — Mic + Call ☕"_s)->id, u"22222222-bbbb"_s);
    // Ignoring case only when nothing matches exactly.
    EXPECT_EQ(findSession(kSessions, u"evening RADIO"_s)->id, u"11111111-aaaa"_s);
    EXPECT_EQ(findSession(kSessions, u"twin"_s)->id, u"55555555-eeee"_s);
}

TEST(Lookup, TwoSessionsOfOneNameAreAskedAboutNotGuessed)
{
    const Result<SessionEntry> found = findSession(kSessions, u"Twin"_s);
    ASSERT_FALSE(found);
    EXPECT_EQ(found.error().exit, ExitCode::NotFound);
    EXPECT_EQ(found.error().code, u"ctl.sessionAmbiguous"_s);
    EXPECT_EQ(found.error().args.value("matches"_L1).toArray(), (QJsonArray{u"33333333-cccc"_s, u"44444444-dddd"_s}));

    const Result<SessionEntry> missing = findSession(kSessions, u"Nowhere"_s);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, u"ctl.sessionNotFound"_s);
}

TEST(Lookup, ABlockIsFoundByIdOrNicknameOrAsTheOnlyOneOfItsKind)
{
    const std::vector<BlockEntry> blocks{
        {u"rec1"_s, u"output.recorder"_s, u"Show"_s},
        {u"rec2"_s, u"output.recorder"_s, QString()},
        {u"sw"_s, u"advanced.inputswitch"_s, u"Mic or line"_s},
    };
    EXPECT_EQ(findBlock(blocks, u"rec2"_s)->id, u"rec2"_s);
    EXPECT_EQ(findBlock(blocks, u"show"_s)->id, u"rec1"_s);
    EXPECT_EQ(findBlock(blocks, QString(), u"advanced.inputswitch"_s)->id, u"sw"_s);
    EXPECT_EQ(findBlock(blocks, QString(), u"output.recorder"_s).error().code, u"ctl.blockAmbiguous"_s);
    EXPECT_EQ(findBlock(blocks, QString(), u"effect.volume"_s).error().code, u"ctl.blockNotFound"_s);
    // The kind limits the search: a Recorder is not an Input Switch.
    EXPECT_EQ(findBlock(blocks, u"rec1"_s, u"advanced.inputswitch"_s).error().code, u"ctl.blockNotFound"_s);
}

TEST(Lookup, AValueIsCheckedAgainstItsSettingAndSentAsItsType)
{
    const QJsonObject gain{{"id"_L1, u"gain"_s}, {"type"_L1, u"float"_s}, {"minimum"_L1, 0.0}, {"maximum"_L1, 2.0}};
    EXPECT_EQ(*valueForParam(gain, u"0.5"_s), QJsonValue(0.5));
    EXPECT_EQ(valueForParam(gain, u"3"_s).error().code, u"ctl.badValue"_s);
    EXPECT_EQ(valueForParam(gain, u"3"_s).error().exit, ExitCode::Usage);
    // A comma is not a decimal point here, whatever the locale says.
    EXPECT_FALSE(valueForParam(gain, u"0,5"_s));

    const QJsonObject bands{{"id"_L1, u"bands"_s}, {"type"_L1, u"int"_s}, {"minimum"_L1, 1}, {"maximum"_L1, 8}};
    EXPECT_EQ(*valueForParam(bands, u"4"_s), QJsonValue(qint64(4)));
    EXPECT_FALSE(valueForParam(bands, u"4.5"_s));

    const QJsonObject bypass{{"id"_L1, u"bypass"_s}, {"type"_L1, u"bool"_s}};
    EXPECT_EQ(*valueForParam(bypass, u"on"_s), QJsonValue(true));
    EXPECT_EQ(*valueForParam(bypass, u"No"_s), QJsonValue(false));
    EXPECT_FALSE(valueForParam(bypass, u"maybe"_s));

    const QJsonObject input{{"id"_L1, u"input"_s}, {"type"_L1, u"enum"_s}, {"choices"_L1, QJsonArray{u"a"_s, u"b"_s}}};
    EXPECT_EQ(*valueForParam(input, u"B"_s), QJsonValue(u"b"_s));
    EXPECT_EQ(valueForParam(input, u"c"_s).error().args.value("choices"_L1).toArray().size(), 2);

    const QJsonObject level{{"id"_L1, u"level"_s}, {"type"_L1, u"float"_s}, {"readOnly"_L1, true}};
    EXPECT_EQ(valueForParam(level, u"1"_s).error().code, u"ctl.readOnly"_s);
}

TEST(Lookup, APluginsOwnSettingIsSentAsTheJsonItReadsAs)
{
    EXPECT_EQ(untypedValue(u"0.25"_s), QJsonValue(0.25));
    EXPECT_EQ(untypedValue(u"true"_s), QJsonValue(true));
    EXPECT_EQ(untypedValue(u"\"12\""_s), QJsonValue(u"12"_s));
    EXPECT_EQ(untypedValue(u"Peak"_s), QJsonValue(u"Peak"_s));
    EXPECT_EQ(untypedValue(u"[1]"_s), QJsonValue(u"[1]"_s));
}
