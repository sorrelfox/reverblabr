// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "bus/engineservice.h"

#include "bus/enginebus.h"
#include "core/enginefacade.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <unistd.h>

#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcEngineService, "reverblabr.bus")

namespace rb::bus
{

namespace
{

/// Every facade signal, relayed onto the bus as the signal of the same name.
///
/// **One QObject with a slot for each signal it has been connected to**,
/// which it does not declare: connecting a signal to an index past the
/// class's own methods and answering the call in qt_metacall() is how
/// QSignalSpy takes a signal it knows nothing about at compile time. The
/// arguments arrive as pointers to values of the signal's parameter types,
/// and leave as a D-Bus signal.
class SignalRelay final : public QObject
{
public:
    SignalRelay(core::EngineFacade *engine, const QDBusConnection &connection)
        : m_connection(connection)
    {
        // **The facade's signals, not an implementation's**: what the
        // interface says, whatever else the object behind it has.
        const QMetaObject &meta = core::EngineFacade::staticMetaObject;
        const int base = QObject::staticMetaObject.methodCount();
        for (int index = meta.methodOffset(); index < meta.methodCount(); ++index) {
            const QMetaMethod method = meta.method(index);
            if (method.methodType() != QMetaMethod::Signal) {
                continue;
            }
            const int slot = base + static_cast<int>(m_signals.size());
            m_signals.push_back(method);
            QMetaObject::connect(engine, index, this, slot, Qt::DirectConnection, nullptr);
        }
    }

    int qt_metacall(QMetaObject::Call call, int id, void **argv) override
    {
        id = QObject::qt_metacall(call, id, argv);
        if (id < 0 || call != QMetaObject::InvokeMetaMethod) {
            return id;
        }
        if (id < static_cast<int>(m_signals.size())) {
            relay(m_signals[static_cast<std::size_t>(id)], argv);
        }
        return id - static_cast<int>(m_signals.size());
    }

private:
    void relay(const QMetaMethod &signal, void **argv)
    {
        QVariantList arguments;
        for (int index = 0; index < signal.parameterCount(); ++index) {
            arguments.append(QVariant(signal.parameterMetaType(index), argv[index + 1]));
        }
        QDBusMessage message = QDBusMessage::createSignal(kObjectPath, kInterfaceName, busNameOf(signal.name()));
        message.setArguments(arguments);
        m_connection.send(message);
    }

    QDBusConnection m_connection;
    std::vector<QMetaMethod> m_signals;
};

} // namespace

class EngineService::Private
{
public:
    core::EngineFacade *engine = nullptr;
    QDBusConnection connection;
    std::unique_ptr<SignalRelay> relay;
    QDBusServiceWatcher watcher;
    QSet<QString> clients;
    QSet<QString> running;
    bool idle = true;
    std::function<int(const QString &)> share;

    explicit Private(const QDBusConnection &connection)
        : connection(connection)
        , watcher(QString{}, connection, QDBusServiceWatcher::WatchForUnregistration)
    {
    }

    /// The facade method a call names, or an invalid one: named in the
    /// facade's own methods, with as many parameters as the call has
    /// arguments.
    /// Answers a call on the telemetry interface: Share(sessionId), with
    /// the memory's descriptor or NoTelemetry.
    void answerTelemetry(const QDBusMessage &message, const QDBusConnection &connection) const
    {
        const QVariantList arguments = message.arguments();
        if (message.member() != u"Share"_s || arguments.size() != 1
            || arguments.first().metaType() != QMetaType::fromType<QString>()) {
            connection.send(message.createErrorReply(QDBusError::UnknownMethod,
                                                     u"No method %1 taking these arguments"_s.arg(message.member())));
            return;
        }
        const QString sessionId = arguments.first().toString();
        const int fd = share ? share(sessionId) : -1;
        if (fd < 0) {
            connection.send(
                message.createErrorReply(QString(kNoTelemetryError), u"No session %1 is open here"_s.arg(sessionId)));
            return;
        }
        // The descriptor object keeps a copy of its own for as long as the
        // reply is on its way; this one has been handed over and is done.
        const QDBusUnixFileDescriptor memory(fd);
        ::close(fd);
        connection.send(message.createReply(QVariant::fromValue(memory)));
    }

    [[nodiscard]] QMetaMethod methodFor(const QDBusMessage &message) const
    {
        const QByteArray name = facadeNameOf(message.member()).toLatin1();
        // **The facade's own methods only**: an implementation's other
        // slots — the mock's test hooks, the engine's connectToPipeWire() —
        // are not the interface, and nobody on the bus may call them. The
        // call still lands in the implementation, since the facade's are
        // virtual.
        const QMetaObject &meta = core::EngineFacade::staticMetaObject;
        for (int index = meta.methodOffset(); index < meta.methodCount(); ++index) {
            const QMetaMethod method = meta.method(index);
            if (method.methodType() != QMetaMethod::Signal && method.access() == QMetaMethod::Public
                && method.name() == name) {
                return method;
            }
        }
        return {};
    }
};

EngineService::EngineService(core::EngineFacade *engine, const QDBusConnection &connection, QObject *parent)
    : QDBusVirtualObject(parent)
    , d(std::make_unique<Private>(connection))
{
    d->engine = engine;
    d->relay = std::make_unique<SignalRelay>(engine, connection);

    const auto update = [this] {
        const bool idle = d->clients.isEmpty() && d->running.isEmpty();
        if (idle != d->idle) {
            d->idle = idle;
            Q_EMIT idleChanged(idle);
        }
    };
    // A client that has gone from the bus is no longer one, however it left:
    // closed, crashed or killed.
    connect(&d->watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, update](const QString &name) {
        d->watcher.removeWatchedService(name);
        d->clients.remove(name);
        update();
    });
    // What is running is read off the facade's own answers, so it is what
    // every client was told.
    connect(engine,
            &core::EngineFacade::stateChanged,
            this,
            [this, update](const QString &sessionId, const QString &state) {
                if (state == core::sessionState::idle || state == core::sessionState::error) {
                    d->running.remove(sessionId);
                } else {
                    d->running.insert(sessionId);
                }
                update();
            });
    connect(engine, &core::EngineFacade::sessionClosed, this, [this, update](const QString &sessionId) {
        d->running.remove(sessionId);
        update();
    });
}

EngineService::~EngineService()
{
    d->connection.unregisterObject(kObjectPath);
}

bool EngineService::publish()
{
    if (!d->connection.registerVirtualObject(kObjectPath, this)) {
        return false;
    }
    if (!d->connection.registerService(kServiceName)) {
        d->connection.unregisterObject(kObjectPath);
        return false;
    }
    return true;
}

void EngineService::setTelemetry(std::function<int(const QString &)> share)
{
    d->share = std::move(share);
}

bool EngineService::isIdle() const
{
    return d->idle;
}

int EngineService::clientCount() const
{
    return static_cast<int>(d->clients.size());
}

int EngineService::runningCount() const
{
    return static_cast<int>(d->running.size());
}

QString EngineService::introspect(const QString &path) const
{
    return path == kObjectPath ? interfaceXml() + telemetryInterfaceXml() : QString{};
}

bool EngineService::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    const bool telemetry = message.interface() == kTelemetryInterfaceName;
    if (message.type() != QDBusMessage::MethodCallMessage || (message.interface() != kInterfaceName && !telemetry)) {
        return false;
    }
    // **Whoever calls is a client** until it leaves the bus.
    const QString sender = message.service();
    if (!sender.isEmpty() && !d->clients.contains(sender)) {
        d->clients.insert(sender);
        d->watcher.addWatchedService(sender);
        if (d->idle) {
            d->idle = false;
            Q_EMIT idleChanged(false);
        }
    }

    if (telemetry) {
        d->answerTelemetry(message, connection);
        return true;
    }

    const QMetaMethod method = d->methodFor(message);
    if (!method.isValid()) {
        qCWarning(lcEngineService) << "refused" << message.member() << "from" << sender;
        connection.send(message.createErrorReply(QDBusError::UnknownMethod, u"No method %1"_s.arg(message.member())));
        return true;
    }
    // **The published signature, exactly** (ADR-0020): an integer where the
    // interface says a string is a mistake in the caller, not something to
    // turn into "42" and act on.
    const QString expected = signatureOf(method);
    QVariantList arguments = message.arguments();
    if (message.signature() != expected || !convertArguments(method, arguments)) {
        qCWarning(lcEngineService) << "refused" << message.member() << message.signature() << "from" << sender;
        connection.send(message.createErrorReply(
            QDBusError::InvalidArgs,
            u"%1 takes (%2), not (%3)"_s.arg(message.member(), expected, message.signature())));
        return true;
    }
    // **Answered before the work is done**: nothing here returns a value,
    // and the answer to a call is a signal (ADR-0003).
    if (message.isReplyRequired()) {
        connection.send(message.createReply());
    }
    invoke(d->engine, method, arguments);
    return true;
}

} // namespace rb::bus
