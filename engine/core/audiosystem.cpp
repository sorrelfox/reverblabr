// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/audiosystem.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace rb::core
{

QString audioSystemToJson(const AudioSystemInfo &info)
{
    const QJsonObject json{
        {"state"_L1, info.state},
        {"server"_L1, info.serverName},
        {"serverVersion"_L1, info.serverVersion},
        {"sessionManager"_L1, info.sessionManagerName},
        {"sessionManagerVersion"_L1, info.sessionManagerVersion},
        {"driver"_L1, info.driver},
        {"rate"_L1, info.rate},
        {"quantum"_L1, info.quantum},
    };
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

AudioSystemInfo audioSystemFromJson(const QString &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    AudioSystemInfo info;
    // **What is absent keeps its default.** A field this build has not
    // learned about is no reason to throw the rest of the answer away.
    if (const QJsonValue value = object.value("state"_L1); value.isString()) {
        info.state = value.toString();
    }
    info.serverName = object.value("server"_L1).toString();
    info.serverVersion = object.value("serverVersion"_L1).toString();
    info.sessionManagerName = object.value("sessionManager"_L1).toString();
    info.sessionManagerVersion = object.value("sessionManagerVersion"_L1).toString();
    info.driver = object.value("driver"_L1).toString();
    info.rate = object.value("rate"_L1).toInt();
    info.quantum = object.value("quantum"_L1).toInt();
    return info;
}

} // namespace rb::core
