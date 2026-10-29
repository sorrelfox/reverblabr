// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A window, as far as the daemon can tell: a process of its own on the bus
// that makes a session, starts it, says `running <id>` once it is, and then
// waits to be killed. "Killing the window does not stop the sound" cannot be
// checked from inside the process that is killed.
//
//   reverblabr-bus-client <session name>

#include "bus/remoteengine.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::fputs("usage: reverblabr-bus-client <session name>\n", stderr);
        return 2;
    }
    rb::bus::RemoteEngine engine;
    QTextStream out(stdout);
    QObject::connect(&engine, &rb::core::EngineFacade::sessionOpened, &engine, [&engine](const QString &sessionId) {
        engine.start(sessionId);
    });
    QObject::connect(&engine,
                     &rb::core::EngineFacade::stateChanged,
                     &engine,
                     [&out](const QString &sessionId, const QString &state) {
                         if (state == rb::core::sessionState::running) {
                             out << "running " << sessionId << Qt::endl;
                         }
                     });
    QObject::connect(&engine, &rb::core::EngineFacade::error, &engine, [](const QString &code, const QString &args) {
        std::fprintf(stderr, "error %s %s\n", qPrintable(code), qPrintable(args));
    });
    engine.createSession(QString::fromLocal8Bit(argv[1]));
    return QCoreApplication::exec();
}
