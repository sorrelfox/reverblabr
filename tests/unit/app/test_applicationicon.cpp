// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The application's icon, and the desktop entry naming it.

#include "applicationicon.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QRegularExpression>
#include <gtest/gtest.h>

using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

/// One application for the whole run, offscreen: an icon is drawn with one.
/// Never destroyed, as in the other app tests.
void ensureApplication()
{
    if (QCoreApplication::instance() != nullptr) {
        return;
    }
    static int argc = 1;
    static char name[] = "reverblabr-app-test";
    static char *argv[] = {name, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    new QGuiApplication(argc, argv);
}

TEST(ApplicationIconTest, TheIconIsThereWithoutAThemeThatHasIt)
{
    ensureApplication();
    // **A build that has not been installed has its icon too**: no theme
    // here has io.github.reverblabr.Reverblabr, so this is the built-in copy.
    ASSERT_FALSE(QIcon::hasThemeIcon(QString(kApplicationIconName)))
        << "this machine has Reverblabr's icon installed; the test means to see the built-in one";
    const QIcon icon = applicationIcon();
    ASSERT_FALSE(icon.isNull());
    for (const int size : {16, 22, 48, 256}) {
        const QImage image = icon.pixmap(size, size).toImage();
        ASSERT_EQ(image.width(), size);
        // The plate's colour at its middle row, well inside its rounded corners.
        EXPECT_GT(qAlpha(image.pixel(size / 2, size / 2)), 200) << "nothing drawn at " << size << " px";
    }
}

TEST(ApplicationIconTest, TheDesktopEntryNamesTheSameIcon)
{
    QFile desktop(QStringLiteral(RB_DESKTOP_FILE));
    ASSERT_TRUE(desktop.open(QIODevice::ReadOnly));
    const QString entry = QString::fromUtf8(desktop.readAll());
    static const QRegularExpression icon(u"^\\[Desktop Entry\\][^[]*^Icon=(\\S+)$"_s,
                                         QRegularExpression::MultilineOption);
    const auto match = icon.match(entry);
    ASSERT_TRUE(match.hasMatch());
    EXPECT_EQ(match.captured(1), kApplicationIconName);
}

} // namespace
