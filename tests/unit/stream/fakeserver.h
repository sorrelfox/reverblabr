// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// A server that takes a stream the way Icecast does, as far as a test needs:
// it reads a source's request, answers it as it is told to, and keeps what
// comes after; a metadata request is answered and remembered. It can hang
// up on the source, as a server that restarts does.

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rb::test
{

class FakeStreamServer
{
public:
    FakeStreamServer()
    {
        m_listen = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const int on = 1;
        (void)::setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        (void)::bind(m_listen, reinterpret_cast<sockaddr *>(&address), sizeof address);
        (void)::listen(m_listen, 8);
        socklen_t length = sizeof address;
        (void)::getsockname(m_listen, reinterpret_cast<sockaddr *>(&address), &length);
        m_port = ntohs(address.sin_port);
        m_thread = std::thread([this] { run(); });
    }

    ~FakeStreamServer()
    {
        m_stopping = true;
        m_thread.join();
        ::close(m_listen);
    }

    FakeStreamServer(const FakeStreamServer &) = delete;
    FakeStreamServer &operator=(const FakeStreamServer &) = delete;

    [[nodiscard]] std::uint16_t port() const
    {
        return m_port;
    }

    /// The status line a source is answered with.
    void setAnswer(std::string answer)
    {
        const std::lock_guard lock(m_mutex);
        m_answer = std::move(answer);
    }

    /// Speaks SHOUTcast's protocol instead: a password on a line, "OK2",
    /// then the source's headers.
    void setShoutcast(std::string password)
    {
        const std::lock_guard lock(m_mutex);
        m_shoutcastPassword = std::move(password);
    }

    /// Answers a title request only after \a delay, as a busy server
    /// might; the stream's audio is read meanwhile.
    void setTitleDelay(std::chrono::milliseconds delay)
    {
        const std::lock_guard lock(m_mutex);
        m_titleDelay = delay;
    }

    /// Hangs up on a SHOUTcast source's password without a word, as a
    /// server that is restarting does.
    void setShoutcastHangsUp(bool hangsUp)
    {
        m_shoutcastHangsUp = hangsUp;
    }

    /// Hangs up on the source that is streaming, once.
    void hangUp()
    {
        m_hangUp = true;
    }

    [[nodiscard]] std::vector<std::string> requests() const
    {
        const std::lock_guard lock(m_mutex);
        return m_requests;
    }

    [[nodiscard]] std::vector<std::string> titleRequests() const
    {
        const std::lock_guard lock(m_mutex);
        return m_titleRequests;
    }

    /// Everything sources have streamed, across connections.
    [[nodiscard]] std::string received() const
    {
        const std::lock_guard lock(m_mutex);
        return m_received;
    }

    [[nodiscard]] std::size_t sources() const
    {
        const std::lock_guard lock(m_mutex);
        return m_requests.size();
    }

private:
    struct Client {
        int fd = -1;
        std::string header;
        bool streaming = false;
        bool loggedIn = false;
        bool icy = false;
        /// A title request's answer, and when it is to be sent.
        std::string answer;
        std::chrono::steady_clock::time_point answerAt{};
    };

    [[nodiscard]] std::string shoutcastPassword() const
    {
        const std::lock_guard lock(m_mutex);
        return m_shoutcastPassword;
    }

    void run()
    {
        std::vector<Client> clients;
        while (!m_stopping) {
            std::vector<pollfd> entries{{m_listen, POLLIN, 0}};
            for (const Client &client : clients) {
                entries.push_back({client.fd, POLLIN, 0});
            }
            const int ready = ::poll(entries.data(), entries.size(), 20);
            // Title answers held back until their time.
            for (Client &client : clients) {
                if (!client.answer.empty() && std::chrono::steady_clock::now() >= client.answerAt) {
                    (void)::send(client.fd, client.answer.data(), client.answer.size(), MSG_NOSIGNAL);
                    ::close(client.fd);
                    client.fd = -1;
                }
            }
            if (ready <= 0) {
                continue;
            }
            if ((entries[0].revents & POLLIN) != 0) {
                const int fd = ::accept4(m_listen, nullptr, nullptr, SOCK_CLOEXEC);
                if (fd >= 0) {
                    Client client;
                    client.fd = fd;
                    clients.push_back(std::move(client));
                }
            }
            for (std::size_t index = 0; index < clients.size(); ++index) {
                Client &client = clients[index];
                if (client.fd < 0) {
                    continue;
                }
                if (client.streaming && m_hangUp.exchange(false)) {
                    ::close(client.fd);
                    client.fd = -1;
                    continue;
                }
                if ((entries.size() > index + 1 ? entries[index + 1].revents : 0) == 0) {
                    continue;
                }
                char buffer[8192];
                const ssize_t got = ::recv(client.fd, buffer, sizeof buffer, 0);
                if (got <= 0) {
                    ::close(client.fd);
                    client.fd = -1;
                    continue;
                }
                if (client.streaming) {
                    const std::lock_guard lock(m_mutex);
                    m_received.append(buffer, static_cast<std::size_t>(got));
                    continue;
                }
                client.header.append(buffer, static_cast<std::size_t>(got));
                if (!client.loggedIn && !shoutcastPassword().empty()) {
                    const std::size_t line = client.header.find("\r\n");
                    if (line == std::string::npos) {
                        continue;
                    }
                    const std::lock_guard lock(m_mutex);
                    if (m_shoutcastHangsUp) {
                        m_requests.push_back(client.header.substr(0, line));
                        ::close(client.fd);
                        client.fd = -1;
                        continue;
                    }
                    if (client.header.substr(0, line) != m_shoutcastPassword) {
                        const std::string answer = "invalid password\r\n";
                        (void)::send(client.fd, answer.data(), answer.size(), MSG_NOSIGNAL);
                        m_requests.push_back(client.header.substr(0, line));
                        ::close(client.fd);
                        client.fd = -1;
                        continue;
                    }
                    const std::string answer = "OK2\r\nicy-caps:11\r\n\r\n";
                    (void)::send(client.fd, answer.data(), answer.size(), MSG_NOSIGNAL);
                    client.header.erase(0, line + 2);
                    client.loggedIn = true;
                    client.icy = true;
                }
                const std::size_t end = client.header.find("\r\n\r\n");
                if (end == std::string::npos) {
                    continue;
                }
                const std::string request = client.header.substr(0, end + 4);
                const std::string rest = client.header.substr(end + 4);
                const std::lock_guard lock(m_mutex);
                if (request.starts_with("GET ")) {
                    m_titleRequests.push_back(request);
                    client.answer = "HTTP/1.0 200 OK\r\nContent-Type: text/xml\r\n\r\n<ok/>";
                    client.answerAt = std::chrono::steady_clock::now() + m_titleDelay;
                    client.header.clear();
                    client.loggedIn = true;
                    continue;
                }
                m_requests.push_back(request);
                if (client.icy) {
                    client.streaming = true;
                    m_received += rest;
                    continue;
                }
                const std::string answer = m_answer + "\r\n\r\n";
                (void)::send(client.fd, answer.data(), answer.size(), MSG_NOSIGNAL);
                if (m_answer.find(" 200 ") != std::string::npos || m_answer.find(" 100 ") != std::string::npos) {
                    client.streaming = true;
                    m_received += rest;
                } else {
                    ::close(client.fd);
                    client.fd = -1;
                }
            }
            std::erase_if(clients, [](const Client &client) { return client.fd < 0; });
        }
        for (const Client &client : clients) {
            ::close(client.fd);
        }
    }

    int m_listen = -1;
    std::uint16_t m_port = 0;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_hangUp{false};
    std::atomic<bool> m_shoutcastHangsUp{false};
    std::chrono::milliseconds m_titleDelay{0};
    mutable std::mutex m_mutex;
    std::string m_answer = "HTTP/1.1 200 OK";
    std::string m_shoutcastPassword;
    std::vector<std::string> m_requests;
    std::vector<std::string> m_titleRequests;
    std::string m_received;
    std::thread m_thread;
};

} // namespace rb::test
