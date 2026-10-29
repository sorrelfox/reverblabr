// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where an engine error ends up: a sentence in the banner, or a badge
// on the block it is about. The banner that draws it is checked in
// tests/qml; what is checked here is which of the two an error goes to and
// what it then says.

#include "appshell.h"
#include "core/mockengine.h"
#include "errormessages.h"
#include "errorsurface.h"

#include <QSignalSpy>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// A session in the mock, made and opened, whose id comes back.
[[nodiscard]] QString openSession(core::MockEngine &engine)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Surface"_s);
    EXPECT_EQ(opened.count(), 1);
    return opened.isEmpty() ? QString{} : opened.back().at(0).toString();
}

} // namespace

TEST(ErrorSurfaceTest, AnErrorAboutTheSessionBecomesASentenceInTheBanner)
{
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);
    EXPECT_FALSE(surface.isVisible());

    QSignalSpy changes(&surface, &ui::ErrorSurface::changed);
    engine.reportError(u"graph.cycle"_s, u"{}"_s);

    EXPECT_TRUE(surface.isVisible());
    EXPECT_EQ(surface.code(), u"graph.cycle"_s);
    EXPECT_EQ(surface.message(), app::errorMessage(u"graph.cycle"_s));
    // **Words, not a code.** The engine's code never reaches a reader.
    EXPECT_FALSE(surface.message().contains(u"graph.cycle"_s));
    EXPECT_FALSE(surface.canRetry()) << "there is nothing to try again about a loop in the wiring";
    EXPECT_GE(changes.count(), 1);
    EXPECT_TRUE(surface.flaggedBlocks().isEmpty());
}

TEST(ErrorSurfaceTest, AReasonIsTranslatedAndNotShownAsTheEnginesOwnWord)
{
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);

    engine.reportError(u"session.cannotRun"_s, uR"({"session":"Podcast","reason":"NodeRefused"})"_s);
    EXPECT_TRUE(surface.isVisible());
    EXPECT_TRUE(surface.message().contains(u"Podcast"_s)) << surface.message().toStdString();
    EXPECT_FALSE(surface.message().contains(u"NodeRefused"_s)) << surface.message().toStdString();
}

TEST(ErrorSurfaceTest, AnErrorThatNamesABlockGoesOnThatBlockAndNotInTheBanner)
{
    // A badge beside the thing that went wrong says more than a sentence at
    // the top of the window, and the canvas reads this to draw it.
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);

    QSignalSpy blocks(&surface, &ui::ErrorSurface::blocksChanged);
    engine.reportError(u"block.unknownType"_s, uR"({"type":"effect.nosuch","block":"b1"})"_s);

    EXPECT_EQ(blocks.count(), 1);
    EXPECT_EQ(surface.flaggedBlocks(), QStringList{u"b1"_s});
    EXPECT_FALSE(surface.badge(u"b1"_s).isEmpty());
    EXPECT_TRUE(surface.badge(u"b1"_s).contains(u"effect.nosuch"_s));
    EXPECT_TRUE(surface.badge(u"b2"_s).isEmpty());
    EXPECT_FALSE(surface.isVisible()) << "a block's trouble took over the session's banner";

    // Twice about the same block is one badge, saying the later thing.
    engine.reportError(u"block.unknownType"_s, uR"({"type":"effect.other","block":"b1"})"_s);
    EXPECT_EQ(surface.flaggedBlocks(), QStringList{u"b1"_s});
    EXPECT_TRUE(surface.badge(u"b1"_s).contains(u"effect.other"_s));

    surface.clearBlock(u"b1"_s);
    EXPECT_TRUE(surface.flaggedBlocks().isEmpty());
    EXPECT_TRUE(surface.badge(u"b1"_s).isEmpty());
}

TEST(ErrorSurfaceTest, TheAudioSystemBeingAwayIsTheBannerAndItOffersARetry)
{
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);
    EXPECT_FALSE(surface.isVisible());

    engine.setAudioSystemRunning(false);
    EXPECT_TRUE(surface.isVisible());
    EXPECT_TRUE(surface.canRetry());
    EXPECT_TRUE(surface.isAboutConnection());
    // It names what is missing, using the name it had while it was there.
    EXPECT_TRUE(surface.message().contains(u"PipeWire"_s)) << surface.message().toStdString();

    // Retrying while it is still away leaves the banner where it is.
    surface.retry();
    EXPECT_TRUE(surface.isVisible());

    // The server comes back, and the Retry is what reconnects.
    engine.setAudioSystemRunning(true);
    EXPECT_TRUE(surface.isVisible()) << "a server that is back is not a connection to it";
    surface.retry();
    EXPECT_FALSE(surface.isVisible());
    EXPECT_FALSE(surface.canRetry());
}

TEST(ErrorSurfaceTest, TheAudioSystemOutranksWhateverElseWentWrong)
{
    // Every other failure while the audio system is away is a consequence
    // of that one, and a person told four things is told nothing.
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);
    surface.setSessionId(openSession(engine));

    engine.setAudioSystemRunning(false);
    const QString connectionMessage = surface.message();

    // Run with nothing to run on: the engine refuses and says why.
    QSignalSpy errors(&engine, &core::EngineFacade::error);
    engine.start(surface.sessionId());
    ASSERT_EQ(errors.count(), 1);
    EXPECT_EQ(errors.back().at(0).toString(), u"session.cannotRun"_s);

    EXPECT_TRUE(surface.isVisible());
    EXPECT_EQ(surface.message(), connectionMessage);
    EXPECT_TRUE(surface.canRetry());
    // The refusal is still remembered, and is what shows once the audio
    // system is back and the banner has nothing better to say.
    engine.setAudioSystemRunning(true);
    surface.retry();
    EXPECT_TRUE(surface.isVisible());
    EXPECT_EQ(surface.code(), u"session.cannotRun"_s);
}

TEST(ErrorSurfaceTest, PuttingAMessageAwayIsNotMendingIt)
{
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);

    engine.reportError(u"graph.cycle"_s, u"{}"_s);
    EXPECT_TRUE(surface.isVisible());
    surface.dismiss();
    EXPECT_FALSE(surface.isVisible());

    // The next thing that goes wrong is worth showing, dismissal or not.
    engine.reportError(u"edit.refused"_s, uR"({"reason":"blockNotFound"})"_s);
    EXPECT_TRUE(surface.isVisible());
    EXPECT_EQ(surface.code(), u"edit.refused"_s);

    // And so is the audio system going away.
    surface.dismiss();
    EXPECT_FALSE(surface.isVisible());
    engine.setAudioSystemRunning(false);
    EXPECT_TRUE(surface.isVisible());
}

TEST(ErrorSurfaceTest, ASessionThatCouldNotBeOpenedIsStillReportedWhenAnotherComesToTheFront)
{
    // The report is about no session in particular — there is no tab for it —
    // so bringing a session to the front is not a reason to forget it.
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);

    engine.openSession(u"00000000-0000-0000-0000-000000000003"_s);
    ASSERT_TRUE(surface.isVisible());
    ASSERT_EQ(surface.code(), u"session.notFound"_s);
    // No name to give, so the id — whole — and never called a name.
    EXPECT_TRUE(surface.message().contains(u"00000000-0000-0000-0000-000000000003"_s))
        << surface.message().toStdString();

    surface.setSessionId(openSession(engine));
    EXPECT_TRUE(surface.isVisible()) << "the report went with the session that came to the front";
    EXPECT_EQ(surface.code(), u"session.notFound"_s);

    surface.dismiss();
    EXPECT_FALSE(surface.isVisible());
}

TEST(ErrorSurfaceTest, WhatWentWrongInAnotherSessionIsNotShownAgainstThisOne)
{
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);
    surface.setSessionId(openSession(engine));

    engine.reportError(u"graph.cycle"_s, u"{}"_s);
    engine.reportError(u"block.unknownType"_s, uR"({"type":"effect.nosuch","block":"b1"})"_s);
    ASSERT_TRUE(surface.isVisible());
    ASSERT_FALSE(surface.flaggedBlocks().isEmpty());

    surface.setSessionId(openSession(engine));
    EXPECT_FALSE(surface.isVisible());
    EXPECT_TRUE(surface.code().isEmpty());
    EXPECT_TRUE(surface.flaggedBlocks().isEmpty());
}

TEST(ErrorSurfaceTest, AVirtualDeviceRefusalIsLeftToTheWindowThatAskedForIt)
{
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);

    // **Said once, where it was asked for**: the Virtual Devices
    // window shows its own refusals beside its list.
    engine.reportError(u"device.virtualNotCreated"_s, uR"({"name":"Every app","reason":"FileNotWritable"})"_s);
    engine.reportError(u"device.virtualRefused"_s, uR"({"device":"Every app","reason":"FileNotWritable"})"_s);
    EXPECT_FALSE(surface.isVisible());

    // **A node PipeWire would not make is not an answer to anything asked**:
    // it can happen with the window closed, so the banner still says it.
    engine.reportError(u"device.virtualNodeRefused"_s, uR"({"device":"Every app"})"_s);
    EXPECT_TRUE(surface.isVisible());
    EXPECT_EQ(surface.code(), u"device.virtualNodeRefused"_s);
}

// A plugin whose host crashed or stopped responding: the badge on its
// tile says which plugin, and that the sound goes around it.
TEST(ErrorSurfaceTest, APluginThatWentDownWearsABadgeSayingSo)
{
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);

    engine.reportError(u"plugin.crashed"_s, uR"({"plugin":"test.gain","block":"p"})"_s);
    EXPECT_EQ(surface.flaggedBlocks(), QStringList{u"p"_s});
    EXPECT_TRUE(surface.badge(u"p"_s).startsWith(u"Plugin crashed"_s)) << surface.badge(u"p"_s).toStdString();
    EXPECT_TRUE(surface.badge(u"p"_s).contains(u"test.gain"_s));
    EXPECT_FALSE(surface.isVisible()) << "a block's trouble is not the banner's";

    engine.reportError(u"plugin.stoppedResponding"_s, uR"({"plugin":"test.gain","block":"p"})"_s);
    EXPECT_TRUE(surface.badge(u"p"_s).startsWith(u"Plugin stopped responding"_s));

    engine.reportError(u"plugin.restartRefused"_s, uR"({"reason":"notAHostedPlugin","block":"p"})"_s);
    EXPECT_TRUE(surface.badge(u"p"_s).contains(u"switched on"_s)) << surface.badge(u"p"_s).toStdString();
}

TEST(ErrorSurfaceTest, AWindowRunningItsOwnEngineSaysWhyAndKeepsSayingItWhicheverSessionIsInFront)
{
    // The window could not reach the background engine and runs its
    // own. It is told before there is a window, and is about no session.
    ui::AppShell::setStartupNotice(u"engine.daemonUnreachable"_s, uR"({"reason":"daemonNotInstalled"})"_s);
    struct Forget {
        ~Forget()
        {
            ui::AppShell::setStartupNotice({});
        }
    } forget;

    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);
    ASSERT_TRUE(surface.isVisible());
    EXPECT_EQ(surface.code(), u"engine.daemonUnreachable"_s);
    EXPECT_TRUE(surface.message().contains(u"isn't installed"_s)) << surface.message().toStdString();
    EXPECT_FALSE(surface.message().contains(u"daemonNotInstalled"_s)) << "the engine's word, not ours";

    surface.setSessionId(openSession(engine));
    EXPECT_TRUE(surface.isVisible()) << "the notice went with the session that came to the front";
    EXPECT_EQ(surface.code(), u"engine.daemonUnreachable"_s);
    surface.setSessionId(openSession(engine));
    EXPECT_EQ(surface.code(), u"engine.daemonUnreachable"_s);

    surface.dismiss();
    EXPECT_FALSE(surface.isVisible());
}

TEST(ErrorSurfaceTest, AWindowOnTheEngineItWasMeantToHaveHasNothingToSayAtStart)
{
    ui::AppShell::setStartupNotice({});
    ui::ErrorSurface surface;
    core::MockEngine engine;
    surface.attach(&engine);
    EXPECT_FALSE(surface.isVisible());
    EXPECT_TRUE(surface.code().isEmpty());
}
