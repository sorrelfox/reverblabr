// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "applicationicon.h"

using namespace Qt::StringLiterals;

namespace rb::ui
{

bool applicationIconInstalled()
{
    return QIcon::hasThemeIcon(QString(kApplicationIconName));
}

QIcon applicationIcon()
{
    // The two drawings the theme gets, in the same order of preference: the
    // one made for 16 px below 22, the full one above.
    static const QIcon bundled = [] {
        QIcon icon;
        icon.addFile(u":/qt/qml/io/github/reverblabr/appicon/16-apps-io.github.reverblabr.Reverblabr.svg"_s,
                     QSize(16, 16));
        icon.addFile(u":/qt/qml/io/github/reverblabr/appicon/sc-apps-io.github.reverblabr.Reverblabr.svg"_s);
        return icon;
    }();
    return QIcon::fromTheme(QString(kApplicationIconName), bundled);
}

} // namespace rb::ui
