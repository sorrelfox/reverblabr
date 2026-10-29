// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "plugin/messages.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace rb::plugin
{

namespace
{

thread_local bool t_wouldBlock = false;

} // namespace

bool sendMessage(int fd, const Message &message)
{
    if (message.payload.size() > kMaxMessageBytes) {
        return false;
    }
    std::vector<char> packet(sizeof(std::uint32_t) + message.payload.size());
    const auto type = static_cast<std::uint32_t>(message.type);
    std::memcpy(packet.data(), &type, sizeof(type));
    std::memcpy(packet.data() + sizeof(type), message.payload.data(), message.payload.size());
    ssize_t sent = 0;
    do {
        // MSG_NOSIGNAL: a host that has gone is an answer, not a SIGPIPE.
        sent = ::send(fd, packet.data(), packet.size(), MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent == static_cast<ssize_t>(packet.size());
}

std::optional<Message> receiveMessage(int fd, bool wait)
{
    t_wouldBlock = false;
    std::vector<char> packet(sizeof(std::uint32_t) + kMaxMessageBytes);
    ssize_t got = 0;
    do {
        got = ::recv(fd, packet.data(), packet.size(), wait ? 0 : MSG_DONTWAIT);
    } while (got < 0 && errno == EINTR);
    if (got < 0 && errno == EAGAIN) { // EWOULDBLOCK is the same number here
        t_wouldBlock = true;
        return std::nullopt;
    }
    if (got < static_cast<ssize_t>(sizeof(std::uint32_t))) {
        return std::nullopt; // closed, or not one of ours
    }
    std::uint32_t type = 0;
    std::memcpy(&type, packet.data(), sizeof(type));
    Message message;
    message.type = static_cast<MessageType>(type);
    message.payload.assign(packet.data() + sizeof(type), static_cast<std::size_t>(got) - sizeof(type));
    return message;
}

bool wouldBlock()
{
    return t_wouldBlock;
}

} // namespace rb::plugin
