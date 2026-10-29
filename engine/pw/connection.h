// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <memory>

struct pw_core;
struct pw_loop;
struct pw_registry;

class QSocketNotifier;
class QTimer;

namespace rb::pw
{

/// Where a connection to PipeWire stands.
enum class ConnectionState : std::uint8_t {
    /// Not started, or stopped.
    Disconnected,
    /// Trying, for the first time or after a wait.
    Connecting,
    /// Connected; the registry is live.
    Connected,
    /// The connection broke and the next attempt is being waited for.
    Waiting,
};

[[nodiscard]] QString connectionStateName(ConnectionState state);

/// The engine's connection to PipeWire, and the thing that keeps it.
///
/// **PipeWire's main loop runs on this object's Qt event loop.** A
/// QSocketNotifier watches the loop's file descriptor and iterates it when it
/// becomes readable, so registry events, replies and state changes all arrive
/// on the thread the engine's state lives on, with no lock and no second
/// thread ([ADR-0008]). The audio thread is PipeWire's own and is not this.
///
/// **It reconnects by itself.** PipeWire or WirePlumber restarting, or the
/// socket going away, moves it to Waiting and it tries again after a delay
/// that doubles up to a limit. Everything built on the old connection is torn
/// down first: the registry mirror empties and fills again, and a caller that
/// held ids has to look them up afresh, which is what the serial is for.
///
/// Create it on the thread it will run on. It is not thread-safe, and nothing
/// here may be called from the audio thread.
class Connection : public QObject
{
    Q_OBJECT

public:
    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;

    /// Starts connecting, and keeps the connection from then on. Returns at
    /// once; watch stateChanged().
    void start();

    /// Disconnects and stops trying.
    void stop();

    [[nodiscard]] ConnectionState state() const noexcept;

    [[nodiscard]] bool isConnected() const noexcept
    {
        return state() == ConnectionState::Connected;
    }

    /// What the server said about itself, once connected: its version and the
    /// name it goes by. Empty before that.
    [[nodiscard]] QString serverVersion() const;
    [[nodiscard]] QString serverName() const;

    /// How many times the connection has come back after breaking. Nothing
    /// depends on it; it belongs in the diagnostics.
    [[nodiscard]] std::uint64_t reconnectCount() const noexcept;

    /// Why the last attempt failed, in English, for the log.
    [[nodiscard]] QString lastError() const;

    /// How long to wait before the first retry, and the longest the wait ever
    /// grows to. It doubles in between. Set before start(); the defaults are
    /// 250 ms and 5 s, and tests shorten them.
    void setRetryDelays(std::chrono::milliseconds first, std::chrono::milliseconds longest);

    /// The server objects, valid only between registryReady() and
    /// registryGone(). For the engine's own PipeWire code; nothing above
    /// engine/pw touches these.
    [[nodiscard]] pw_loop *loop() const noexcept;
    [[nodiscard]] pw_core *core() const noexcept;
    [[nodiscard]] pw_registry *registry() const noexcept;

    /// The id of **this connection's** client object in the server's graph,
    /// or 0 before the server has given it one.
    ///
    /// A process may hold more than one connection — a test harness does, and
    /// so does any library in the process that talks to PipeWire itself — so
    /// "the client with our process id" names more than one. This names
    /// exactly one.
    [[nodiscard]] std::uint32_t clientId() const;

    /// Asks the server for a round trip and returns its sequence number, or
    /// -1 when there is no connection.
    ///
    /// The server answers requests in the order they were made, so synced()
    /// with this number means everything asked for before it — every bind,
    /// every link — has been dealt with and its answers are already in.
    [[nodiscard]] int sync();

    /// Runs one pass of PipeWire's loop, for a test that has no Qt event loop
    /// running. Ordinary use needs nothing of the sort: the notifier does it.
    void iterateForTest(std::chrono::milliseconds timeout);

Q_SIGNALS:
    void stateChanged(rb::pw::ConnectionState state);
    /// The registry is live and has announced nothing yet. A mirror starts
    /// filling from here.
    void registryReady();
    /// The connection is going away. A mirror empties here; the proxies it
    /// holds are already invalid after this returns.
    void registryGone();
    /// The connection broke on its own, as opposed to stop() being called.
    void connectionLost(const QString &reason);
    /// The server has answered the round trip with this sequence number.
    void synced(int seq);

public:
    /// The implementation. Named in the header only so that the file which
    /// defines it can reach it from PipeWire's C callbacks, which are free
    /// functions and cannot be members.
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::pw
