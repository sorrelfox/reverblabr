// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **Copy Diagnostics reaches a real clipboard**: the one path the
// other tests hand to a stand-in. A binary of its own, so that it can make
// its application on the offscreen platform before anything else makes one
// — whose clipboard lives in this process and nowhere else — and refuse to
// go on if it could not: this test never writes to the clipboard of the
// desktop it runs on.

#include "core/mockengine.h"
#include "diagnostics.h"

#include <QClipboard>
#include <QGuiApplication>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] QGuiApplication &offscreenApplication()
{
    static QGuiApplication *const application = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char name[] = "reverblabr-diagnostics-clipboard-test";
        static char *argv[] = {name, nullptr};
        return new QGuiApplication(argc, argv);
    }();
    return *application;
}

} // namespace

TEST(DiagnosticsClipboardTest, CopyDiagnosticsPutsTheBundleOnTheClipboard)
{
    const QGuiApplication &application = offscreenApplication();
    ASSERT_EQ(application.platformName(), u"offscreen"_s) << "refusing to touch a desktop's clipboard";
    QClipboard *clipboard = QGuiApplication::clipboard();
    ASSERT_NE(clipboard, nullptr);
    clipboard->setText(u"something else"_s);

    core::MockEngine engine;
    Diagnostics diagnostics;
    diagnostics.attach(&engine);
    diagnostics.refresh();
    ASSERT_TRUE(diagnostics.isReady());
    diagnostics.copyBundle();

    EXPECT_EQ(clipboard->text(), diagnostics.bundle());
    EXPECT_TRUE(clipboard->text().startsWith(u"Reverblabr diagnostics"_s)) << clipboard->text().toStdString();
}
