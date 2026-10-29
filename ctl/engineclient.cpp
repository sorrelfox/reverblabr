// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "engineclient.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QElapsedTimer>
#include <QJsonDocument>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace rb::ctl
{

namespace
{

/// The signals that answer the tool's questions.
constexpr std::array kSignals{
    "SessionsListed",
    "SessionOpened",
    "SessionClosed",
    "SessionSnapshot",
    "SessionPatch",
    "StateChanged",
    "BlockCatalogueChanged",
    "RecordingFileStarted",
    "TimersListed",
    "TimerFired",
    "Error",
};

/// The D-Bus errors that mean there is no engine to ask, rather than an
/// engine that answered: nobody has the name and nobody can start it, or
/// the start failed.
[[nodiscard]] bool meansUnreachable(const QString &name)
{
    return name == "org.freedesktop.DBus.Error.ServiceUnknown"_L1
        || name == "org.freedesktop.DBus.Error.NameHasNoOwner"_L1
        || name == "org.freedesktop.DBus.Error.Disconnected"_L1
        || name.startsWith("org.freedesktop.DBus.Error.Spawn."_L1);
}

[[nodiscard]] QJsonObject argsOf(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

[[nodiscard]] QJsonObject objectOf(const QVariant &json)
{
    return QJsonDocument::fromJson(json.toString().toUtf8()).object();
}

} // namespace

EngineClient::EngineClient(QDBusConnection connection, std::chrono::milliseconds timeout)
    : m_connection(std::move(connection))
    , m_timeout(timeout)
{
    for (const char *name : kSignals) {
        // Any sender: the engine's unique name changes when it restarts, and
        // the object path and interface are what say it is the engine.
        m_connection.connect(QString(),
                             QString::fromLatin1(kObjectPath),
                             QString::fromLatin1(kInterfaceName),
                             QString::fromLatin1(name),
                             this,
                             SLOT(heard(QDBusMessage)));
    }
}

void EngineClient::heard(const QDBusMessage &message)
{
    m_heard.push_back({message.member(), message.arguments()});
}

Result<void> EngineClient::reach(bool activate)
{
    if (!m_connection.isConnected()) {
        return fail(ExitCode::Unreachable, u"ctl.unreachable"_s, {{"reason"_L1, u"noSessionBus"_s}});
    }
    if (!activate && !m_connection.interface()->isServiceRegistered(QString::fromLatin1(kServiceName)).value()) {
        return fail(ExitCode::Unreachable, u"ctl.unreachable"_s, {{"reason"_L1, u"notRunning"_s}});
    }
    return {};
}

Result<void> EngineClient::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kServiceName),
                                                          QString::fromLatin1(kObjectPath),
                                                          QString::fromLatin1(kInterfaceName),
                                                          method);
    message.setArguments(arguments);
    const QDBusMessage reply = m_connection.call(message, QDBus::Block, static_cast<int>(m_timeout.count()));
    if (reply.type() != QDBusMessage::ErrorMessage) {
        return {};
    }
    if (reply.errorName() == "org.freedesktop.DBus.Error.NoReply"_L1
        || reply.errorName() == "org.freedesktop.DBus.Error.Timeout"_L1) {
        return fail(ExitCode::Timeout, u"ctl.timeout"_s, {{"method"_L1, method}});
    }
    if (meansUnreachable(reply.errorName())) {
        return fail(ExitCode::Unreachable, u"ctl.unreachable"_s, {{"reason"_L1, reply.errorName()}});
    }
    // UnknownMethod, InvalidArgs: an engine older than this tool, or a bug.
    return fail(
        ExitCode::Refused, u"ctl.dbusError"_s, {{"name"_L1, reply.errorName()}, {"message"_L1, reply.errorMessage()}});
}

Result<EngineClient::Heard> EngineClient::await(const Answer &answer, const ErrorFilter &mine)
{
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        for (; m_next < m_heard.size(); ++m_next) {
            const Heard &heard = m_heard[m_next];
            if (heard.signal == "Error"_L1) {
                const QString code = heard.arguments.value(0).toString();
                const QJsonObject args = argsOf(heard.arguments.value(1).toString());
                if (mine && mine(code, args)) {
                    ++m_next;
                    return fail(ExitCode::Refused, code, args);
                }
                continue;
            }
            if (answer(heard)) {
                return m_heard[m_next++];
            }
        }
        const auto left = m_timeout.count() - clock.elapsed();
        if (left <= 0) {
            return fail(ExitCode::Timeout, u"ctl.timeout"_s);
        }
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, static_cast<int>(std::min<qint64>(left, 50)));
    }
}

Result<EngineClient::Heard>
EngineClient::ask(const QString &method, const QVariantList &arguments, const Answer &answer, const ErrorFilter &mine)
{
    // **Only what comes after the call is its answer.** A signal still
    // waiting from an earlier question, or about something else, is passed
    // over.
    QCoreApplication::processEvents();
    m_next = m_heard.size();
    if (Result<void> called = call(method, arguments); !called) {
        return std::unexpected(called.error());
    }
    Result<Heard> heard = await(answer, mine);
    if (!heard && heard.error().code == "ctl.timeout"_L1) {
        heard.error().args.insert("method"_L1, method);
    }
    return heard;
}

Result<QJsonArray> EngineClient::listSessions()
{
    const Result<Heard> heard =
        ask(u"ListSessions"_s, {}, [](const Heard &heard) { return heard.signal == "SessionsListed"_L1; });
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return QJsonDocument::fromJson(heard->arguments.value(0).toString().toUtf8()).array();
}

Result<QJsonArray> EngineClient::listTimers()
{
    const Result<Heard> heard =
        ask(u"ListTimers"_s, {}, [](const Heard &heard) { return heard.signal == "TimersListed"_L1; });
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return QJsonDocument::fromJson(heard->arguments.value(0).toString().toUtf8()).array();
}

Result<EngineClient::Fired> EngineClient::fireTimer(const QString &timerId, bool scheduled)
{
    const Result<Heard> heard = ask(
        u"FireTimer"_s,
        {timerId, scheduled},
        [&](const Heard &heard) {
            return heard.signal == "TimerFired"_L1 && heard.arguments.value(0).toString() == timerId;
        },
        [&](const QString &code, const QJsonObject &args) {
            return (code == "timer.notFound"_L1 || code == "timer.late"_L1)
                && args.value("timer"_L1).toString() == timerId;
        });
    if (!heard) {
        // No such timer is a name that matches nothing, as a session's is.
        if (heard.error().code == "timer.notFound"_L1) {
            return fail(ExitCode::NotFound, heard.error().code, heard.error().args);
        }
        return std::unexpected(heard.error());
    }
    return Fired{heard->arguments.value(1).toString(), heard->arguments.value(2).toBool()};
}

namespace
{

[[nodiscard]] auto aboutSession(const QString &sessionId)
{
    return [sessionId](const QString &code, const QJsonObject &args) {
        return (code == "session.notFound"_L1 || code == "session.corruptFile"_L1)
            && (args.value("session"_L1).toString() == sessionId
                || args.value("file"_L1).toString().contains(sessionId));
    };
}

} // namespace

Result<void> EngineClient::openSession(const QString &sessionId)
{
    const Result<Heard> heard = ask(
        u"OpenSession"_s,
        {sessionId},
        [&](const Heard &heard) {
            return heard.signal == "SessionOpened"_L1 && heard.arguments.value(0).toString() == sessionId;
        },
        aboutSession(sessionId));
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return {};
}

Result<void> EngineClient::closeSession(const QString &sessionId, const QString &name)
{
    const ErrorFilter notFound = aboutSession(sessionId);
    const Result<Heard> heard = ask(
        u"CloseSession"_s,
        {sessionId},
        [&](const Heard &heard) {
            return heard.signal == "SessionClosed"_L1 && heard.arguments.value(0).toString() == sessionId;
        },
        [&](const QString &code, const QJsonObject &args) {
            const QString session = args.value("session"_L1).toString();
            return notFound(code, args) || (code == "session.notSaved"_L1 && (session == sessionId || session == name));
        });
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return {};
}

Result<QJsonObject> EngineClient::snapshot(const QString &sessionId)
{
    const Result<Heard> heard = ask(
        u"Resynchronise"_s,
        {sessionId},
        [&](const Heard &heard) {
            return heard.signal == "SessionSnapshot"_L1 && heard.arguments.value(0).toString() == sessionId;
        },
        aboutSession(sessionId));
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return objectOf(heard->arguments.value(2));
}

Result<QJsonObject> EngineClient::catalogue()
{
    const Result<Heard> heard =
        ask(u"GetBlockCatalogue"_s, {}, [](const Heard &heard) { return heard.signal == "BlockCatalogueChanged"_L1; });
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return objectOf(heard->arguments.value(0));
}

Result<QString> EngineClient::start(const QString &sessionId, const QString &name)
{
    const ErrorFilter notFound = aboutSession(sessionId);
    const Result<Heard> heard = ask(
        u"Start"_s,
        {sessionId},
        [&](const Heard &heard) {
            // **Where it settles**, not the "starting" on the way there.
            const QString state = heard.arguments.value(1).toString();
            return heard.signal == "StateChanged"_L1 && heard.arguments.value(0).toString() == sessionId
                && (state == "running"_L1 || state == "error"_L1 || state == "idle"_L1);
        },
        [&](const QString &code, const QJsonObject &args) {
            // The engine names the session in session.cannotRun by its name.
            const QString session = args.value("session"_L1).toString();
            return notFound(code, args)
                || (code == "session.cannotRun"_L1 && (session == sessionId || session == name));
        });
    if (!heard) {
        return std::unexpected(heard.error());
    }
    const QString state = heard->arguments.value(1).toString();
    if (state != "running"_L1) {
        return fail(ExitCode::Refused, u"session.cannotRun"_s, {{"session"_L1, name}, {"state"_L1, state}});
    }
    return state;
}

Result<QString> EngineClient::stop(const QString &sessionId)
{
    const Result<Heard> heard = ask(
        u"Stop"_s,
        {sessionId},
        [&](const Heard &heard) {
            const QString state = heard.arguments.value(1).toString();
            return heard.signal == "StateChanged"_L1 && heard.arguments.value(0).toString() == sessionId
                && (state == "idle"_L1 || state == "error"_L1);
        },
        aboutSession(sessionId));
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return heard->arguments.value(1).toString();
}

Result<QString> EngineClient::split(const QString &sessionId, const QString &blockId)
{
    const ErrorFilter notFound = aboutSession(sessionId);
    const Result<Heard> heard = ask(
        u"ControlRecording"_s,
        {sessionId, blockId, u"split"_s},
        [&](const Heard &heard) {
            return heard.signal == "RecordingFileStarted"_L1 && heard.arguments.value(0).toString() == sessionId
                && heard.arguments.value(1).toString() == blockId;
        },
        [&](const QString &code, const QJsonObject &args) {
            return notFound(code, args)
                || ((code == "recording.refused"_L1 || code == "recording.failed"_L1)
                    && args.value("block"_L1).toString() == blockId);
        });
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return heard->arguments.value(2).toString();
}

Result<void>
EngineClient::setParam(const QString &sessionId, const QString &blockId, const QString &param, const QJsonValue &value)
{
    const QJsonObject edit{{"op"_L1, u"setParam"_s}, {"blockId"_L1, blockId}, {"key"_L1, param}, {"value"_L1, value}};
    const ErrorFilter notFound = aboutSession(sessionId);
    const Result<Heard> heard = ask(
        u"ApplyEdit"_s,
        {sessionId, QString::fromUtf8(QJsonDocument(edit).toJson(QJsonDocument::Compact))},
        [&](const Heard &heard) {
            if (heard.signal != "SessionPatch"_L1 || heard.arguments.value(0).toString() != sessionId) {
                return false;
            }
            const QJsonObject patch = objectOf(heard.arguments.value(2));
            return patch.value("op"_L1).toString() == "setParam"_L1 && patch.value("blockId"_L1).toString() == blockId
                && patch.value("key"_L1).toString() == param;
        },
        // edit.refused names no session: one arriving between this call and
        // its patch is taken as this edit's.
        [&](const QString &code, const QJsonObject &args) {
            return notFound(code, args) || code == "edit.refused"_L1;
        });
    if (!heard) {
        return std::unexpected(heard.error());
    }
    return {};
}

} // namespace rb::ctl
