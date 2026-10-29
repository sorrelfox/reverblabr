// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/audiosystem.h"
#include "core/enginefacade.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

namespace rb::ui
{

/// What the window is running on: the audio server, the session manager and
/// whether the engine can reach either of them.
///
/// **It asks the engine and never the system.** The GUI has no PipeWire
/// connection of its own and is not going to grow one to read a version
/// number; the engine is the client, so the engine is what knows ([ADR-0003]).
/// That also means this works the same against the mock, which is how the
/// window's behaviour with no audio system at all can be built and tested.
///
/// **Everything here can be empty**, and the interface shows a dash rather
/// than a guess: a server that has not answered yet has no version, and one
/// that is not there has nothing at all.
class AudioSystem : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether the engine is attached to the audio system right now. **What
    /// the banner goes by.**
    Q_PROPERTY(bool connected READ isConnected NOTIFY changed)
    /// One of rb::core::audioSystemState: disconnected, connecting,
    /// connected or waiting. A code, not a word to read.
    Q_PROPERTY(QString state READ state NOTIFY changed)
    /// What the server calls itself, for example "PipeWire", and the
    /// version it reported.
    Q_PROPERTY(QString serverName READ serverName NOTIFY changed)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY changed)
    /// The session manager whose policy the graph follows, for example
    /// "WirePlumber", and its version.
    Q_PROPERTY(QString sessionManagerName READ sessionManagerName NOTIFY changed)
    Q_PROPERTY(QString sessionManagerVersion READ sessionManagerVersion NOTIFY changed)
    /// The node driving the graph, by a name a person would read.
    Q_PROPERTY(QString driver READ driver NOTIFY changed)
    /// The rate and the quantum the graph runs at, whether or not a session
    /// of ours is running, and what that quantum is worth in
    /// milliseconds. Nought until the engine has said.
    Q_PROPERTY(int rate READ rate NOTIFY changed)
    Q_PROPERTY(int quantum READ quantum NOTIFY changed)
    Q_PROPERTY(qreal cycleMs READ cycleMs NOTIFY changed)

public:
    explicit AudioSystem(QObject *parent = nullptr);
    ~AudioSystem() override;

    static AudioSystem *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade, and asks it at once what it is running on.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString serverName() const;
    [[nodiscard]] QString serverVersion() const;
    [[nodiscard]] QString sessionManagerName() const;
    [[nodiscard]] QString sessionManagerVersion() const;
    [[nodiscard]] QString driver() const;
    [[nodiscard]] int rate() const;
    [[nodiscard]] int quantum() const;
    [[nodiscard]] qreal cycleMs() const;

    /// Asks the engine to try the connection again now. **What Retry
    /// calls.** Nothing happens here; the answer arrives as a change.
    Q_INVOKABLE void reconnect();

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
