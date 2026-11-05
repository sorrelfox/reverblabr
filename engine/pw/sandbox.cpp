// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/sandbox.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace rb::pw
{

bool inFlatpak(const QString &marker)
{
    return QFileInfo::exists(marker);
}

QString pipeWireConfigHome(bool sandboxed)
{
    if (!sandboxed) {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
    const QString host = qEnvironmentVariable("HOST_XDG_CONFIG_HOME");
    return host.isEmpty() ? QDir::homePath() + u"/.config"_s : host;
}

} // namespace rb::pw
