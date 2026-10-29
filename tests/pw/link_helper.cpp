// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A separate process that makes links with the engine's link manager and then
// waits to be killed. The test that kills it checks that none of its links
// are left in the graph, which is the promise object.linger = false makes and
// which cannot be checked from inside the process that made them.
//
// Usage: reverblabr-link-helper <output node.name> <input node.name>
// It prints "linked <n>" on stdout once the links are there.

#include "pw/connection.h"
#include "pw/linkmanager.h"
#include "pw/registrymirror.h"

#include <QCoreApplication>
#include <QString>
#include <QTimer>

#include <cstdio>

using namespace rb::pw;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fputs("usage: reverblabr-link-helper <output node.name> <input node.name>\n", stderr);
        return 2;
    }
    const QString outputName = QString::fromUtf8(argv[1]);
    const QString inputName = QString::fromUtf8(argv[2]);

    Connection connection;
    RegistryMirror mirror(connection);
    LinkManager manager(connection, mirror);
    manager.setSettleTime(std::chrono::milliseconds{10});

    DesiredLink left;
    left.from.node.nodeName = outputName;
    left.from.node.excludeReplayed = false;
    left.from.channel = QStringLiteral("FL");
    left.to.node.nodeName = inputName;
    left.to.node.excludeReplayed = false;
    left.to.channel = QStringLiteral("FL");

    DesiredLink right = left;
    right.from.channel = QStringLiteral("FR");
    right.to.channel = QStringLiteral("FR");

    manager.setDesiredLinks({left, right});

    QObject::connect(&manager, &LinkManager::reconciled, &app, [&manager](int, int) {
        static bool announced = false;
        if (!announced && manager.ownedLinkCount() > 0) {
            announced = true;
            std::printf("linked %d\n", manager.ownedLinkCount());
            std::fflush(stdout);
        }
    });

    connection.start();

    // Never returns on its own: the test kills it.
    QTimer giveUp;
    giveUp.setSingleShot(true);
    QObject::connect(&giveUp, &QTimer::timeout, &app, &QCoreApplication::quit);
    giveUp.start(120'000);
    return QCoreApplication::exec();
}
