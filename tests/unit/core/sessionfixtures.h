// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/session.h"
#include "core/sessionstore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUuid>

#include <ostream>

namespace rb::test
{

/// A session with something in every field, so that a round-trip test cannot
/// pass by leaving a field at its default at both ends.
///
/// The positions are deliberately not whole numbers: that is what the check
/// on decimal separators needs to see in the file.
inline core::Session fullSession()
{
    core::Session session;
    session.id = QUuid::fromString(QStringLiteral("0d9c8b6e-5f3a-4c1e-9b7d-2a6f4e8c1b35"));
    session.name = QStringLiteral("Podcast");
    session.notes = QStringLiteral("Two microphones and the browser.\nSecond line.");
    session.settings.autoConnect = false;
    session.settings.sampleRate = QStringLiteral("96000");
    session.settings.latency = QStringLiteral("low");
    session.settings.autoRun = QStringLiteral("login");

    core::BlockInstance source;
    source.id = QStringLiteral("b1");
    source.type = QStringLiteral("source.application");
    source.nickname = QStringLiteral("Browser");
    source.enabled = true;
    source.position = QPointF{40.5, 120.25};
    source.params = QJsonObject{
        {QStringLiteral("match"), QJsonObject{{QStringLiteral("binary"), QStringLiteral("firefox")}}},
        {QStringLiteral("includeMicrophone"), false},
    };
    source.preset = QStringLiteral("factory.browser");
    session.blocks.append(source);

    core::BlockInstance meter;
    meter.id = QStringLiteral("b2");
    meter.type = QStringLiteral("meter.peakrms");
    meter.enabled = false;
    meter.position = QPointF{220.0, 120.5};
    session.blocks.append(meter);

    core::BlockInstance output;
    output.id = QStringLiteral("b3");
    output.type = QStringLiteral("output.device");
    output.position = QPointF{400.75, 120.0};
    output.params = QJsonObject{{QStringLiteral("device"), QStringLiteral("default")}};
    session.blocks.append(output);

    session.wires.append(core::Wire{QStringLiteral("b1"), 0, QStringLiteral("b2"), 0});
    session.wires.append(core::Wire{QStringLiteral("b1"), 1, QStringLiteral("b2"), 1});
    session.wires.append(core::Wire{QStringLiteral("b2"), 0, QStringLiteral("b3"), 0});

    // Fields this version does not use, which a later one might.
    session.timers = QJsonArray{QJsonObject{{QStringLiteral("at"), QStringLiteral("2026-09-20T08:00:00Z")}}};
    session.automations = QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("fade")}}};
    return session;
}

} // namespace rb::test

namespace rb::core
{

/// Prints a session as the JSON a file would hold, so that a failed
/// comparison says which field differs instead of dumping 192 bytes of
/// object. GoogleTest finds this by argument-dependent lookup.
inline void PrintTo(const Session &session, std::ostream *out)
{
    *out << QJsonDocument(sessionToJson(session)).toJson(QJsonDocument::Indented).toStdString();
}

} // namespace rb::core
