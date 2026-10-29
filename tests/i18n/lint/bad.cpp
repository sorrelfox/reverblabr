// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Fixture for tools/lint-i18n.py: the lint must report every line that ends
// with a marker comment, once per marker, and nothing else.

#include <KLocalizedString>
#include <QLabel>
#include <QMainWindow>

void badStrings(QLabel *label, QMainWindow *window)
{
    label->setText(QStringLiteral("Hello")); // expect-finding
    window->setWindowTitle(u"Reverblabr"_s); // expect-finding
    label->setToolTip("Plain literal"); // expect-finding
    label->setText(i18nc("@label", "Level: %1", 3) + QStringLiteral(" dB")); // expect-finding
    label->setText(QObject::tr("Qt translation")); // expect-finding expect-finding
}
