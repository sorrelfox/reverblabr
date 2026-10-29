// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/connection.h"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace rb::stream
{

namespace
{

/// How long one wait lasts before \a cancel is looked at again.
constexpr std::chrono::milliseconds kSlice{50};

/// Waits for \a events on \a fd until \a deadline or \a cancel, a slice at
/// a time: the events that came, zero when time ran out, or an error.
[[nodiscard]] std::expected<short, StreamError>
waitFor(int fd, short events, std::chrono::steady_clock::time_point deadline, const std::atomic<bool> &cancel)
{
    for (;;) {
        if (cancel.load(std::memory_order_relaxed)) {
            return std::unexpected(StreamError::Closed);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return 0;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd entry{fd, events, 0};
        const int ready = ::poll(&entry, 1, static_cast<int>(std::min(left, kSlice).count()));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(StreamError::ConnectFailed);
        }
        if (ready > 0) {
            return entry.revents;
        }
    }
}

} // namespace

/// One name being looked up, shared by whoever asked and the thread that
/// asks the C library. **Whoever asked may give up and go**: the thread
/// finishes on its own, and the last of the two to let go frees the answer.
struct Connection::Lookup {
    std::mutex mutex;
    std::condition_variable done;
    bool finished = false;
    int result = EAI_FAIL;
    addrinfo *addresses = nullptr;

    ~Lookup()
    {
        if (addresses != nullptr) {
            ::freeaddrinfo(addresses);
        }
    }
};

std::expected<std::shared_ptr<const Connection::Lookup>, StreamError>
Connection::lookUp(const std::string &host,
                   std::uint16_t port,
                   std::chrono::steady_clock::time_point deadline,
                   const std::atomic<bool> &cancel)
{
    // **On a thread of its own, left behind if it takes too long.**
    // getaddrinfo() cannot be called off, and a resolver that does not
    // answer holds whoever called it for as long as its own timeouts say:
    // seconds, and a session that is stopping must not wait for it.
    auto lookup = std::make_shared<Lookup>();
    try {
        std::thread([lookup, host, service = std::to_string(port)] {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
            addrinfo *found = nullptr;
            const int result = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
            const std::lock_guard lock(lookup->mutex);
            lookup->result = result;
            lookup->addresses = found;
            lookup->finished = true;
            lookup->done.notify_all();
        }).detach();
    } catch (const std::system_error &) {
        return std::unexpected(StreamError::ResolveFailed);
    }
    std::unique_lock lock(lookup->mutex);
    while (!lookup->finished) {
        if (cancel.load(std::memory_order_relaxed)) {
            return std::unexpected(StreamError::Closed);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(StreamError::TimedOut);
        }
        lookup->done.wait_for(lock, kSlice);
    }
    if (lookup->result != 0 || lookup->addresses == nullptr) {
        return std::unexpected(StreamError::ResolveFailed);
    }
    return std::shared_ptr<const Lookup>(lookup);
}

Connection::~Connection()
{
    close();
}

Connection::Connection(Connection &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1))
{
}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this != &other) {
        close();
        m_fd = std::exchange(other.m_fd, -1);
    }
    return *this;
}

void Connection::close() noexcept
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

std::expected<Connection, StreamError> Connection::open(const std::string &host,
                                                        std::uint16_t port,
                                                        std::chrono::milliseconds timeout,
                                                        const std::atomic<bool> &cancel)
{
    if (host.empty() || port == 0) {
        return std::unexpected(StreamError::NotConfigured);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::expected<std::shared_ptr<const Lookup>, StreamError> looked = lookUp(host, port, deadline, cancel);
    if (!looked) {
        return std::unexpected(looked.error());
    }
    const addrinfo *found = (*looked)->addresses;
    std::size_t count = 0;
    for (const addrinfo *address = found; address != nullptr; address = address->ai_next) {
        ++count;
    }

    StreamError last = StreamError::ConnectFailed;
    for (const addrinfo *address = found; address != nullptr; address = address->ai_next, --count) {
        // **Each address gets its share of the time left**, so a dead IPv6
        // route does not use up the time an IPv4 one would have answered in.
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            last = StreamError::TimedOut;
            break;
        }
        const auto share = (deadline - now) / static_cast<long>(std::max<std::size_t>(count, 1));
        const auto until = now + share;
        const int fd = ::socket(address->ai_family, address->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            continue;
        }
        Connection connection(fd);
        // The audio goes out as it is made, in small writes: Nagle's wait
        // for a fuller packet would only add to the delay.
        const int on = 1;
        (void)::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            return connection;
        }
        if (errno != EINPROGRESS) {
            continue;
        }
        const std::expected<short, StreamError> ready = waitFor(fd, POLLOUT, until, cancel);
        if (!ready) {
            return std::unexpected(ready.error());
        }
        if (*ready == 0) {
            last = StreamError::TimedOut;
            continue;
        }
        int error = 0;
        socklen_t length = sizeof error;
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0) {
            return connection;
        }
        last = StreamError::ConnectFailed;
    }
    return std::unexpected(last);
}

std::expected<void, StreamError>
Connection::send(std::span<const char> bytes, std::chrono::milliseconds timeout, const std::atomic<bool> &cancel)
{
    if (m_fd < 0) {
        return std::unexpected(StreamError::Closed);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!bytes.empty()) {
        const ssize_t sent = ::send(m_fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent > 0) {
            bytes = bytes.subspan(static_cast<std::size_t>(sent));
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && errno == EAGAIN) {
            const std::expected<short, StreamError> ready = waitFor(m_fd, POLLOUT, deadline, cancel);
            if (!ready) {
                return std::unexpected(ready.error());
            }
            if (*ready == 0) {
                return std::unexpected(StreamError::TimedOut);
            }
            if ((*ready & (POLLERR | POLLHUP)) != 0 && (*ready & POLLOUT) == 0) {
                return std::unexpected(StreamError::Closed);
            }
            continue;
        }
        return std::unexpected(StreamError::Closed);
    }
    return {};
}

std::expected<bool, StreamError> Connection::receiveSome(std::string &received,
                                                         std::chrono::steady_clock::time_point deadline,
                                                         const std::atomic<bool> &cancel,
                                                         std::size_t maxBytes)
{
    if (m_fd < 0) {
        return false;
    }
    const std::expected<short, StreamError> ready = waitFor(m_fd, POLLIN, deadline, cancel);
    if (!ready) {
        return std::unexpected(ready.error());
    }
    if (*ready == 0) {
        return std::unexpected(StreamError::TimedOut);
    }
    std::array<char, 2048> buffer{};
    const ssize_t got = ::recv(m_fd, buffer.data(), buffer.size(), 0);
    if (got < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return true;
        }
        return false;
    }
    if (got == 0) {
        return false;
    }
    if (received.size() + static_cast<std::size_t>(got) > maxBytes) {
        return std::unexpected(StreamError::BadResponse);
    }
    received.append(buffer.data(), static_cast<std::size_t>(got));
    return true;
}

bool Connection::peerClosed()
{
    if (m_fd < 0) {
        return true;
    }
    pollfd entry{m_fd, POLLIN, 0};
    if (::poll(&entry, 1, 0) <= 0) {
        return false;
    }
    if ((entry.revents & (POLLERR | POLLHUP)) != 0) {
        return true;
    }
    // Whatever the server says now is not wanted; reading it is how its
    // closing shows.
    std::array<char, 512> buffer{};
    const ssize_t got = ::recv(m_fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
    return got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR);
}

} // namespace rb::stream
