// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "failure.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>

#include <chrono>
#include <functional>
#include <vector>

namespace rb::ctl
{

/// Where the engine is on the session bus: the public API, `Engine1`
/// (ADR-0020, docs/dbus-api.md). The tool uses nothing else.
inline constexpr char kServiceName[] = "io.github.reverblabr.Engine";
inline constexpr char kObjectPath[] = "/io/github/reverblabr/Engine";
inline constexpr char kInterfaceName[] = "io.github.reverblabr.Engine1";

/// The engine, asked one thing at a time and waited for.
///
/// **No method of the API answers.** A call says what should happen and a
/// signal says what did, so every question here is a call and a wait for
/// the signal that answers it, or for the Error signal that refuses it, or
/// for the time to run out. The signals are subscribed to before the first
/// call, and only what arrives after a call counts as its answer.
///
/// **An Error is taken as the answer only when it is about this call.**
/// Every client on the bus hears every Error, and one may be about some
/// other client's session; each question says which errors are its own, by
/// their code and, where the engine gives one, the session or block they
/// name.
class EngineClient : public QObject
{
    Q_OBJECT

public:
    EngineClient(QDBusConnection connection, std::chrono::milliseconds timeout);

    /// Whether there is an engine to ask. Without `activate`, an engine
    /// that is not already running is as good as none: the bus is not asked
    /// to start one (shell completion must not start the engine).
    [[nodiscard]] Result<void> reach(bool activate);

    [[nodiscard]] Result<QJsonArray> listSessions();
    [[nodiscard]] Result<void> openSession(const QString &sessionId);
    [[nodiscard]] Result<void> closeSession(const QString &sessionId, const QString &name);
    /// The whole session, as `Resynchronise` sends it again.
    [[nodiscard]] Result<QJsonObject> snapshot(const QString &sessionId);
    [[nodiscard]] Result<QJsonObject> catalogue();
    /// Runs it, and says the state it settled in.
    [[nodiscard]] Result<QString> start(const QString &sessionId, const QString &name);
    [[nodiscard]] Result<QString> stop(const QString &sessionId);
    /// Ends a running Recorder's file and starts the next; the new file.
    [[nodiscard]] Result<QString> split(const QString &sessionId, const QString &blockId);
    /// One setting, as one step the window can undo.
    /// Every session's timers, as TimersListed carries them.
    [[nodiscard]] Result<QJsonArray> listTimers();
    /// What a timer did when fired: its session, and whether it started it
    /// or left it running as it was.
    struct Fired {
        QString sessionId;
        bool started = true;
    };
    /// Does what the timer does when its time comes. \a scheduled is a fire
    /// from its systemd timer, which the engine refuses when it is late.
    [[nodiscard]] Result<Fired> fireTimer(const QString &timerId, bool scheduled);
    [[nodiscard]] Result<void>
    setParam(const QString &sessionId, const QString &blockId, const QString &param, const QJsonValue &value);

private Q_SLOTS:
    void heard(const QDBusMessage &message);

private:
    struct Heard {
        QString signal;
        QVariantList arguments;
    };
    /// Whether an Error, by its code and arguments, is about the call.
    using ErrorFilter = std::function<bool(const QString &code, const QJsonObject &args)>;
    using Answer = std::function<bool(const Heard &heard)>;

    [[nodiscard]] Result<void> call(const QString &method, const QVariantList &arguments);
    [[nodiscard]] Result<Heard> await(const Answer &answer, const ErrorFilter &mine);
    [[nodiscard]] Result<Heard>
    ask(const QString &method, const QVariantList &arguments, const Answer &answer, const ErrorFilter &mine = {});

    QDBusConnection m_connection;
    std::chrono::milliseconds m_timeout;
    std::vector<Heard> m_heard;
    std::size_t m_next = 0;
};

} // namespace rb::ctl
