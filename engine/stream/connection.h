// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/protocol.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

namespace rb::stream
{

/// A TCP connection to a server, for the thread that streams.
///
/// **Every wait can be called off.** Connecting, sending and waiting for an
/// answer each give up after their time, and also as soon as \a cancel is
/// set, looking at it every few tens of milliseconds: a session that stops
/// must not wait for a server on the other side of the world to answer.
/// Looking up the host's name, which the C library cannot be told to stop,
/// runs on a thread of its own that is left behind when the wait is
/// called off.
///
/// Nothing is sent with SIGPIPE: a server that hangs up is an error to
/// report, not a signal that ends the process.
class Connection
{
public:
    Connection() = default;
    ~Connection();
    Connection(Connection &&other) noexcept;
    Connection &operator=(Connection &&other) noexcept;
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    /// Connects to \a host at \a port, trying each address it has in turn.
    [[nodiscard]] static std::expected<Connection, StreamError> open(const std::string &host,
                                                                     std::uint16_t port,
                                                                     std::chrono::milliseconds timeout,
                                                                     const std::atomic<bool> &cancel);

    /// Sends all of \a bytes, or says why it could not.
    [[nodiscard]] std::expected<void, StreamError>
    send(std::span<const char> bytes, std::chrono::milliseconds timeout, const std::atomic<bool> &cancel);

    /// Reads what arrives until \a complete says it is a whole answer, the
    /// server closes, or the time is up. \a complete is given what has
    /// arrived and whether the server has closed. More than \a maxBytes is
    /// not an answer a source waits for, and is a bad one.
    template <typename Complete>
    [[nodiscard]] std::expected<std::string, StreamError> receive(Complete complete,
                                                                  std::chrono::milliseconds timeout,
                                                                  const std::atomic<bool> &cancel,
                                                                  std::size_t maxBytes = 16384)
    {
        std::string received;
        bool closed = false;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!complete(received, closed)) {
            if (closed) {
                return std::unexpected(received.empty() ? StreamError::Closed : StreamError::BadResponse);
            }
            const std::expected<bool, StreamError> more = receiveSome(received, deadline, cancel, maxBytes);
            if (!more) {
                return std::unexpected(more.error());
            }
            closed = !*more;
        }
        return received;
    }

    /// **Whether the server has hung up**, without waiting: a server that
    /// takes a stream sends nothing back, so anything readable is it
    /// closing, or an error it says before it does.
    [[nodiscard]] bool peerClosed();

    [[nodiscard]] bool isOpen() const noexcept
    {
        return m_fd >= 0;
    }

    void close() noexcept;

private:
    struct Lookup;
    /// The host's addresses, looked up on a thread that is left to finish
    /// by itself if \a cancel or the deadline comes first.
    [[nodiscard]] static std::expected<std::shared_ptr<const Lookup>, StreamError>
    lookUp(const std::string &host,
           std::uint16_t port,
           std::chrono::steady_clock::time_point deadline,
           const std::atomic<bool> &cancel);

    explicit Connection(int fd) noexcept
        : m_fd(fd)
    {
    }

    /// Adds what has arrived to \a received: false once the server has
    /// closed.
    [[nodiscard]] std::expected<bool, StreamError> receiveSome(std::string &received,
                                                               std::chrono::steady_clock::time_point deadline,
                                                               const std::atomic<bool> &cancel,
                                                               std::size_t maxBytes);

    int m_fd = -1;
};

} // namespace rb::stream
