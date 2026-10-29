// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/connection.h"

#include "pw/logging.h"

#include <QSocketNotifier>
#include <QTimer>
#include <pipewire/pipewire.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

using namespace Qt::StringLiterals;

namespace rb::pw
{

QString connectionStateName(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        return u"disconnected"_s;
    case ConnectionState::Connecting:
        return u"connecting"_s;
    case ConnectionState::Connected:
        return u"connected"_s;
    case ConnectionState::Waiting:
        return u"waiting"_s;
    }
    return u"disconnected"_s;
}

class Connection::Private
{
public:
    explicit Private(Connection *owner)
        : q(owner)
    {
    }

    Connection *q = nullptr;

    ConnectionState state = ConnectionState::Disconnected;
    bool wanted = false; ///< start() was called and stop() has not been

    pw_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;

    spa_hook coreListener{};
    bool coreListenerActive = false;

    QSocketNotifier *notifier = nullptr;
    QTimer *retryTimer = nullptr;

    std::chrono::milliseconds firstDelay{250};
    std::chrono::milliseconds longestDelay{5000};
    std::chrono::milliseconds nextDelay{250};

    /// Set while pw_loop_iterate() is running. Nothing may destroy the loop,
    /// the core or the notifier from inside it: PipeWire is still walking the
    /// messages it is delivering, and would come back to freed memory.
    bool iterating = false;
    bool teardownWanted = false;
    bool teardownLost = false;
    QString teardownReason;

    std::uint64_t reconnects = 0;
    QString serverVersion;
    QString serverName;
    QString lastError;

    void setState(ConnectionState next)
    {
        if (state == next) {
            return;
        }
        state = next;
        Q_EMIT q->stateChanged(next);
    }

    /// Opens the loop and tries to reach the server. Leaves the state at
    /// Connected or Waiting.
    void attempt();

    /// Takes everything down, telling anyone who was using the registry
    /// first. \a lost says whether the connection broke on its own.
    void teardown(bool lost, const QString &reason);

    /// Arranges the next attempt, with the wait doubling each time.
    void scheduleRetry();

    void onCoreInfo(const pw_core_info *info);
    void onCoreError(std::uint32_t id, int seq, int result, const char *message);

    /// Takes the connection down, now or as soon as the loop has finished
    /// delivering what it is in the middle of, and arranges the next attempt.
    void requestTeardown(bool lost, const QString &reason);
};

namespace
{

/// The core's events, shared by every connection. The listener's user data is
/// the Private that owns it.
const pw_core_events &coreEvents()
{
    static const pw_core_events events = [] {
        pw_core_events result{};
        result.version = PW_VERSION_CORE_EVENTS;
        result.info = [](void *data, const pw_core_info *info) {
            static_cast<Connection::Private *>(data)->onCoreInfo(info);
        };
        result.error = [](void *data, std::uint32_t id, int seq, int result, const char *message) {
            static_cast<Connection::Private *>(data)->onCoreError(id, seq, result, message);
        };
        result.done = [](void *data, std::uint32_t id, int seq) {
            if (id == PW_ID_CORE) {
                Connection *const connection = static_cast<Connection::Private *>(data)->q;
                Q_EMIT connection->synced(seq);
            }
        };
        return result;
    }();
    return events;
}

/// pw_init() is global and must happen once before anything else.
void initialisePipeWireOnce()
{
    static const bool done = [] {
        pw_init(nullptr, nullptr);
        return true;
    }();
    Q_UNUSED(done)
}

QString propOf(const spa_dict *props, const char *key)
{
    if (props == nullptr) {
        return {};
    }
    const char *value = spa_dict_lookup(props, key);
    return value == nullptr ? QString{} : QString::fromUtf8(value);
}

} // namespace

void Connection::Private::onCoreInfo(const pw_core_info *info)
{
    if (info == nullptr) {
        return;
    }
    serverVersion = info->version == nullptr ? QString{} : QString::fromUtf8(info->version);
    serverName = info->name == nullptr ? QString{} : QString::fromUtf8(info->name);
    if (serverName.isEmpty()) {
        serverName = propOf(info->props, PW_KEY_CORE_NAME);
    }
}

void Connection::Private::onCoreError(std::uint32_t id, int seq, int result, const char *message)
{
    Q_UNUSED(seq)
    const QString text = message == nullptr ? QString{} : QString::fromUtf8(message);
    if (id != PW_ID_CORE) {
        // An error about one object, not about the connection. The object's
        // own proxy deals with it; there is nothing to reconnect.
        qCDebug(RB_PW) << "PipeWire reported an error on object" << id << result << text;
        return;
    }
    if (result == -ENOENT && text.startsWith(u"unknown resource"_s)) {
        // **About one object, though it comes on the core**: the
        // server was asked to do something with an object it has already
        // taken away — a link of ours to a plugin host that was killed, let
        // go of a moment after the server removed it with the host. The
        // connection is as good as it was; tearing it down took every
        // session with it.
        qCDebug(RB_PW) << "PipeWire no longer had an object we named:" << text;
        return;
    }
    qCWarning(RB_PW) << "PipeWire connection error:" << result << text;
    requestTeardown(true, text.isEmpty() ? QString::fromUtf8(std::strerror(-result)) : text);
}

void Connection::Private::requestTeardown(bool lost, const QString &reason)
{
    if (iterating) {
        teardownWanted = true;
        teardownLost = lost;
        teardownReason = reason;
        return;
    }
    teardown(lost, reason);
    scheduleRetry();
}

void Connection::Private::attempt()
{
    setState(ConnectionState::Connecting);
    initialisePipeWireOnce();

    loop = pw_loop_new(nullptr);
    if (loop == nullptr) {
        lastError = u"the PipeWire loop could not be made"_s;
        scheduleRetry();
        return;
    }
    pw_loop_enter(loop);

    // **RB_PW_NO_REALTIME** leaves PipeWire's rt module out of this
    // context, so no thread of ours asks rtkit for real-time. It is
    // how a test runs the data loop the way rtkit leaves it when it
    // refuses, and how anybody can tell a problem real-time scheduling
    // causes from one it does not.
    pw_properties *contextProperties = nullptr;
    if (qEnvironmentVariableIsSet("RB_PW_NO_REALTIME")) {
        contextProperties = pw_properties_new("module.rt", "false", nullptr);
    }
    context = pw_context_new(loop, contextProperties, 0);
    if (context == nullptr) {
        lastError = u"the PipeWire context could not be made"_s;
        teardown(false, {});
        scheduleRetry();
        return;
    }

    core = pw_context_connect(context, nullptr, 0);
    if (core == nullptr) {
        lastError = QString::fromUtf8(std::strerror(errno));
        qCInfo(RB_PW) << "PipeWire is not there yet:" << lastError;
        teardown(false, {});
        scheduleRetry();
        return;
    }

    pw_core_add_listener(core, &coreListener, &coreEvents(), this);
    coreListenerActive = true;

    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    if (registry == nullptr) {
        lastError = u"the PipeWire registry could not be reached"_s;
        teardown(false, {});
        scheduleRetry();
        return;
    }

    // The loop's descriptor becomes readable when any of its sources is
    // ready. Iterating it here is what makes PipeWire's events arrive on this
    // thread's Qt event loop.
    notifier = new QSocketNotifier(pw_loop_get_fd(loop), QSocketNotifier::Read, q);
    QObject::connect(notifier, &QSocketNotifier::activated, q, [this] {
        iterating = true;
        const int result = pw_loop_iterate(loop, 0);
        iterating = false;

        // Anything that wanted the connection taken down while the loop was
        // delivering messages waited until here, where the loop is between
        // two passes and nothing of PipeWire's is still in use.
        if (teardownWanted) {
            teardownWanted = false;
            teardown(teardownLost, teardownReason);
            scheduleRetry();
            return;
        }
        if (result < 0 && result != -EINTR && state == ConnectionState::Connected) {
            qCWarning(RB_PW) << "the PipeWire loop stopped:" << result;
            teardown(true, QString::fromUtf8(std::strerror(-result)));
            scheduleRetry();
        }
    });

    lastError.clear();
    nextDelay = firstDelay;
    setState(ConnectionState::Connected);
    Q_EMIT q->registryReady();
}

void Connection::Private::teardown(bool lost, const QString &reason)
{
    const bool hadRegistry = registry != nullptr;
    if (hadRegistry) {
        // Said before anything is destroyed, so a mirror can let its proxies
        // go while they are still valid.
        Q_EMIT q->registryGone();
    }

    delete notifier;
    notifier = nullptr;

    if (registry != nullptr) {
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
        registry = nullptr;
    }
    if (coreListenerActive) {
        spa_hook_remove(&coreListener);
        coreListenerActive = false;
    }
    if (core != nullptr) {
        pw_core_disconnect(core);
        core = nullptr;
    }
    if (context != nullptr) {
        pw_context_destroy(context);
        context = nullptr;
    }
    if (loop != nullptr) {
        pw_loop_leave(loop);
        pw_loop_destroy(loop);
        loop = nullptr;
    }
    serverVersion.clear();
    serverName.clear();

    if (lost) {
        lastError = reason;
        Q_EMIT q->connectionLost(reason);
    }
}

void Connection::Private::scheduleRetry()
{
    if (!wanted) {
        setState(ConnectionState::Disconnected);
        return;
    }
    setState(ConnectionState::Waiting);
    retryTimer->start(static_cast<int>(nextDelay.count()));
    // Doubling, so a server that is not coming back is not asked about every
    // quarter of a second for ever.
    nextDelay = std::min(longestDelay, nextDelay * 2);
}

Connection::Connection(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    d->retryTimer = new QTimer(this);
    d->retryTimer->setSingleShot(true);
    connect(d->retryTimer, &QTimer::timeout, this, [this] {
        if (d->wanted) {
            ++d->reconnects;
            d->attempt();
        }
    });
}

Connection::~Connection()
{
    d->wanted = false;
    d->teardown(false, {});
}

void Connection::start()
{
    if (d->wanted) {
        return;
    }
    d->wanted = true;
    d->nextDelay = d->firstDelay;
    d->attempt();
}

void Connection::stop()
{
    if (!d->wanted) {
        return;
    }
    d->wanted = false;
    d->retryTimer->stop();
    d->teardown(false, {});
    d->setState(ConnectionState::Disconnected);
}

ConnectionState Connection::state() const noexcept
{
    return d->state;
}

QString Connection::serverVersion() const
{
    return d->serverVersion;
}

QString Connection::serverName() const
{
    return d->serverName;
}

std::uint64_t Connection::reconnectCount() const noexcept
{
    return d->reconnects;
}

QString Connection::lastError() const
{
    return d->lastError;
}

void Connection::setRetryDelays(std::chrono::milliseconds first, std::chrono::milliseconds longest)
{
    d->firstDelay = std::max(std::chrono::milliseconds{1}, first);
    d->longestDelay = std::max(d->firstDelay, longest);
    d->nextDelay = d->firstDelay;
}

pw_loop *Connection::loop() const noexcept
{
    return d->loop;
}

pw_core *Connection::core() const noexcept
{
    return d->core;
}

pw_registry *Connection::registry() const noexcept
{
    return d->registry;
}

std::uint32_t Connection::clientId() const
{
    if (d->core == nullptr) {
        return 0;
    }
    pw_client *client = pw_core_get_client(d->core);
    if (client == nullptr) {
        return 0;
    }
    const std::uint32_t id = pw_proxy_get_bound_id(reinterpret_cast<pw_proxy *>(client));
    return id == SPA_ID_INVALID ? 0 : id;
}

int Connection::sync()
{
    if (d->core == nullptr) {
        return -1;
    }
    // The server answers every request in the order it was made, so a reply
    // to this one means everything asked for before it has been dealt with.
    return pw_core_sync(d->core, PW_ID_CORE, 0);
}

void Connection::iterateForTest(std::chrono::milliseconds timeout)
{
    if (d->loop != nullptr) {
        pw_loop_iterate(d->loop, static_cast<int>(timeout.count()));
    }
}

} // namespace rb::pw
