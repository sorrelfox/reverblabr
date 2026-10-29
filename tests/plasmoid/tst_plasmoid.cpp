// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The panel widget's representations against an engine of the test's own,
// on a private session bus (see CMakeLists.txt). **Never the desktop's
// bus**: the fake takes the engine's name, which on a desktop would put it
// in place of the person's engine, so it refuses to start where it would
// find a real one.

#include "core/sharedtelemetry.h"

#include <KLocalizedQmlContext>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QDBusVirtualObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSet>
#include <QStringList>
#include <QtQuickTest>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

using namespace Qt::StringLiterals;

namespace
{

constexpr QLatin1StringView kServiceName{"io.github.reverblabr.Engine"};
constexpr QLatin1StringView kObjectPath{"/io/github/reverblabr/Engine"};
constexpr QLatin1StringView kInterfaceName{"io.github.reverblabr.Engine1"};
constexpr QLatin1StringView kTelemetryInterfaceName{"io.github.reverblabr.Telemetry"};

} // namespace

/// **An engine the test speaks for**: the few members of the public API the
/// widget uses, answered by signals as reverblabrd answers them, and the
/// telemetry handshake, with memory of the real layout that a test fills.
///
/// It keeps count of who has called it and is still on the bus, as the
/// daemon does, which is how a test sees whether the widget would keep a
/// real engine alive.
class FakeEngine : public QDBusVirtualObject
{
    Q_OBJECT
    /// Whether it has the engine's name now.
    Q_PROPERTY(bool onBus READ onBus NOTIFY changed)
    /// Every call, as `Member arg…`, in order.
    Q_PROPERTY(QStringList calls READ calls NOTIFY changed)
    /// The unique names that have called it and are still on the bus.
    Q_PROPERTY(int clientCount READ clientCount NOTIFY changed)

public:
    [[nodiscard]] bool onBus() const
    {
        return m_onBus;
    }
    [[nodiscard]] QStringList calls() const
    {
        return m_calls;
    }
    [[nodiscard]] int clientCount() const
    {
        return static_cast<int>(m_clients.size());
    }

    /// Takes the engine's name, on a connection of its own.
    Q_INVOKABLE bool appear()
    {
        if (m_onBus) {
            return true;
        }
        m_connectionName = u"fake-engine-%1"_s.arg(++m_generation);
        QDBusConnection connection = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName);
        if (!connection.registerVirtualObject(QString(kObjectPath), this)
            || !connection.registerService(QString(kServiceName))) {
            return false;
        }
        m_watcher =
            std::make_unique<QDBusServiceWatcher>(QString(), connection, QDBusServiceWatcher::WatchForUnregistration);
        connect(m_watcher.get(), &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
            m_watcher->removeWatchedService(name);
            m_clients.remove(name);
            Q_EMIT changed();
        });
        // **What a daemon remembers it had open, it opens again, stopped.**
        for (auto &[id, session] : m_sessions) {
            if (session.state != u"closed"_s) {
                session.state = u"idle"_s;
            }
        }
        m_onBus = true;
        Q_EMIT changed();
        return true;
    }

    /// Leaves the bus, as a daemon does when it exits.
    Q_INVOKABLE void disappear()
    {
        if (!m_onBus) {
            return;
        }
        m_watcher.reset();
        QDBusConnection::disconnectFromBus(m_connectionName);
        m_clients.clear();
        m_onBus = false;
        Q_EMIT changed();
    }

    /// Back to no sessions and no calls, on the bus.
    Q_INVOKABLE void reset()
    {
        m_sessions.clear();
        m_calls.clear();
        m_refuseStart.clear();
        appear();
        Q_EMIT changed();
    }

    Q_INVOKABLE void clearCalls()
    {
        m_calls.clear();
        Q_EMIT changed();
    }

    /// A session on disk, `closed`, or open in \a state.
    Q_INVOKABLE void addSession(const QString &id, const QString &name, const QString &state)
    {
        Session &session = m_sessions[id];
        session.name = name;
        session.state = state;
        if (state != u"closed"_s) {
            open(session);
        }
    }

    /// Says the list, as it does when a window asks for it.
    Q_INVOKABLE void sayList()
    {
        QJsonArray sessions;
        for (const auto &[id, session] : m_sessions) {
            sessions.append(QJsonObject{
                {u"id"_s, id},
                {u"name"_s, session.name},
                {u"state"_s, session.state},
                {u"sources"_s, QJsonArray{}},
                {u"recordings"_s, 0},
                {u"autoRun"_s, u"off"_s},
            });
        }
        say(u"SessionsListed"_s, {QString::fromUtf8(QJsonDocument(sessions).toJson(QJsonDocument::Compact))});
    }

    /// Changes a session's state as somebody else's call would.
    Q_INVOKABLE void setState(const QString &id, const QString &state)
    {
        m_sessions[id].state = state;
        say(u"StateChanged"_s, {id, state});
    }

    /// Publishes one block at \a peak (a gain) into the session's memory.
    Q_INVOKABLE void publishPeak(const QString &id, double peak)
    {
        Session &session = m_sessions[id];
        if (!session.telemetry) {
            return;
        }
        rb::core::TelemetrySnapshot snapshot;
        snapshot.frame = ++session.frame;
        snapshot.running = true;
        snapshot.blockCount = 1;
        std::strncpy(snapshot.blocks[0].id, "in", sizeof(snapshot.blocks[0].id) - 1);
        snapshot.blocks[0].channelCount = 2;
        snapshot.blocks[0].peak[0] = static_cast<float>(peak);
        snapshot.blocks[0].peak[1] = static_cast<float>(peak) / 2.0f;
        snapshot.blocks[0].active = peak > 0.0;
        session.telemetry->writableSlot()->store(snapshot);
    }

    Q_INVOKABLE void startRecording(const QString &id, const QString &path)
    {
        say(u"RecordingFileStarted"_s, {id, u"recorder"_s, path});
    }

    Q_INVOKABLE void finishRecording(const QString &id, const QString &path)
    {
        say(u"RecordingFileFinished"_s, {id, u"recorder"_s, path, u"{}"_s});
    }

    /// The next Start of \a id is refused, as a session with nowhere to
    /// play is.
    Q_INVOKABLE void refuseStart(const QString &id)
    {
        m_refuseStart.insert(id);
    }

    [[nodiscard]] QString introspect(const QString &) const override
    {
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.type() != QDBusMessage::MethodCallMessage) {
            return false;
        }
        const QString sender = message.service();
        if (!sender.isEmpty() && !m_clients.contains(sender)) {
            m_clients.insert(sender);
            m_watcher->addWatchedService(sender);
        }
        QStringList call{message.member()};
        for (const QVariant &argument : message.arguments()) {
            call.append(argument.toString());
        }
        m_calls.append(call.join(u' '));
        Q_EMIT changed();

        const QString member = message.member();
        const QString id = message.arguments().value(0).toString();
        if (message.interface() == kTelemetryInterfaceName && member == u"Share"_s) {
            const auto found = m_sessions.find(id);
            if (found == m_sessions.end() || !found->second.telemetry) {
                connection.send(message.createErrorReply(u"io.github.reverblabr.Error.NoTelemetry"_s, id));
                return true;
            }
            const int fd = found->second.telemetry->shareReadOnly();
            connection.send(message.createReply(QVariant::fromValue(QDBusUnixFileDescriptor(fd))));
            ::close(fd);
            return true;
        }
        connection.send(message.createReply());
        if (member == u"ListSessions"_s) {
            sayList();
        } else if (member == u"OpenSession"_s) {
            const auto found = m_sessions.find(id);
            if (found == m_sessions.end()) {
                sayError(u"session.notFound"_s, id);
            } else {
                open(found->second);
                found->second.state = u"idle"_s;
                say(u"SessionOpened"_s, {id});
            }
        } else if (member == u"Start"_s) {
            if (m_refuseStart.remove(id)) {
                sayError(u"session.cannotRun"_s, id);
            } else {
                setState(id, u"running"_s);
            }
        } else if (member == u"Stop"_s) {
            setState(id, u"idle"_s);
        }
        return true;
    }

Q_SIGNALS:
    void changed();

private:
    struct Session {
        QString name;
        QString state;
        std::optional<rb::core::SharedTelemetry> telemetry;
        std::uint64_t frame = 0;
    };

    static void open(Session &session)
    {
        if (!session.telemetry) {
            auto created = rb::core::SharedTelemetry::create();
            if (created) {
                session.telemetry.emplace(std::move(*created));
            }
        }
    }

    void say(const QString &signal, const QVariantList &arguments)
    {
        if (!m_onBus) {
            return;
        }
        QDBusMessage message = QDBusMessage::createSignal(QString(kObjectPath), QString(kInterfaceName), signal);
        message.setArguments(arguments);
        QDBusConnection(m_connectionName).send(message);
    }

    void sayError(const QString &code, const QString &sessionId)
    {
        const QJsonObject args{{u"session"_s, sessionId}, {u"reason"_s, u"test"_s}};
        say(u"Error"_s, {code, QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))});
    }

    bool m_onBus = false;
    int m_generation = 0;
    QString m_connectionName;
    std::unique_ptr<QDBusServiceWatcher> m_watcher;
    QSet<QString> m_clients;
    QStringList m_calls;
    QSet<QString> m_refuseStart;
    std::map<QString, Session> m_sessions;
};

class Setup : public QObject
{
    Q_OBJECT

public:
    Setup()
    {
        // **Only on a bus of its own**: an engine already on this bus is
        // a person's, and the fake must never stand in for it.
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || bus.interface()->isServiceRegistered(QString(kServiceName))) {
            std::fputs("tst_plasmoid runs only on a private session bus with no Reverblabr engine on it\n", stderr);
            std::exit(1);
        }
    }

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImportPath(QStringLiteral(RB_QML_IMPORT_DIR));
        // **i18n() as Plasma gives it to the widget**: a localized context
        // in the widget's translation domain.
        KLocalizedQmlContext *context = KLocalization::setupLocalizedContext(engine);
        context->setTranslationDomain(QStringLiteral(RB_PLASMOID_DOMAIN));
        qmlRegisterSingletonInstance("ReverblabrTest", 1, 0, "FakeEngine", &m_engine);
    }

private:
    FakeEngine m_engine;
};

QUICK_TEST_MAIN_WITH_SETUP(plasmoid, Setup)

#include "tst_plasmoid.moc"
