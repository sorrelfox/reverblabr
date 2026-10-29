// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Fixture for tools/lint-i18n.py: nothing in this file may be flagged.

#include <KLocalizedString>
#include <QLabel>
#include <QMainWindow>

void goodStrings(QLabel *label, QMainWindow *window, int level)
{
    label->setText(i18nc("@label", "Hello"));
    window->setWindowTitle(i18nc("@title:window", "Reverblabr"));
    label->setToolTip(i18ncp("@info:tooltip", "%1 file", "%1 files", level));
    label->setText(QString::number(level));
    label->setText(QStringLiteral("•"));
    label->setObjectName(QStringLiteral("levelLabel"));
    label->setProperty("kind", QStringLiteral("meter"));
    label->setText(QStringLiteral("Frame time")); // no-i18n: developer overlay
}
