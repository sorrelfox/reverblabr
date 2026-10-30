// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// When Auto Run runs: the Settings menu's choice, and the words it is
// written in, which the daemon reads as it starts (test_autorun.cpp holds
// the daemon to the same text).

#include "autorunsetting.h"

#include <KConfigGroup>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

TEST(AutoRunSettingTest, OnceALoginIsTheDefaultAndTheChoiceIsWrittenAsAWord)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(u"reverblabrrc"_s);
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    ui::AutoRunSetting::setConfig(config);

    {
        ui::AutoRunSetting setting;
        EXPECT_FALSE(setting.everyEngineStart()) << "a first run does not run what the person stopped";
        const QSignalSpy changed(&setting, &ui::AutoRunSetting::everyEngineStartChanged);
        setting.setEveryEngineStart(true);
        setting.setEveryEngineStart(true);
        EXPECT_EQ(changed.count(), 1);
    }
    // **The text the daemon reads**, with no KDE Frameworks of its own.
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("[AutoRun]\nStart=every-engine-start\n"));

    // Remembered, and put back.
    {
        ui::AutoRunSetting setting;
        EXPECT_TRUE(setting.everyEngineStart());
        setting.setEveryEngineStart(false);
    }
    config->reparseConfiguration();
    EXPECT_EQ(config->group(u"AutoRun"_s).readEntry(u"Start"_s, QString()), u"once-a-login"_s);

    // A word from a later version is the default.
    KConfigGroup group = config->group(u"AutoRun"_s);
    group.writeEntry(u"Start"_s, u"something-later"_s);
    group.sync();
    EXPECT_FALSE(ui::AutoRunSetting().everyEngineStart());
    ui::AutoRunSetting::setConfig({});
}
