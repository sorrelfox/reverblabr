// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "plugin/messages.h"

#include <poll.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <vector>

namespace rb::plugin
{

namespace
{

thread_local bool t_wouldBlock = false;

} // namespace

namespace
{

/// The most of a message one packet carries: well under what a socket's
/// default buffer takes whole (about 208 KiB on Linux), so a message of any
/// size up to kMaxMessageBytes goes as packets the kernel never refuses.
constexpr std::size_t kChunkBytes = 60 * 1024;
/// A packet's head: the message's type, and whether more of it follows.
constexpr std::size_t kHeadBytes = 2 * sizeof(std::uint32_t);
/// How long the rest of a message, or room to send it, is waited for once
/// it has begun: the other end sends it all at once, or has gone.
constexpr int kRestOfMessageMs = 2000;

[[nodiscard]] bool waitFor(int fd, short events)
{
    pollfd ready{fd, events, 0};
    int got = 0;
    do {
        got = ::poll(&ready, 1, kRestOfMessageMs);
    } while (got < 0 && errno == EINTR);
    return got > 0 && (ready.revents & events) != 0;
}

} // namespace

void widenSocket(int fd)
{
    // **Asked for, not counted on**: the kernel grants up to what the
    // system allows an unprivileged process (net.core.wmem_max), which is
    // often less; messages are sent in packets that fit either way.
    const int wanted = static_cast<int>(kMaxMessageBytes);
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &wanted, sizeof(wanted));
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &wanted, sizeof(wanted));
}

bool sendMessage(int fd, const Message &message)
{
    if (message.payload.size() > kMaxMessageBytes) {
        return false;
    }
    const auto type = static_cast<std::uint32_t>(message.type);
    std::vector<char> packet(kHeadBytes + kChunkBytes);
    std::size_t at = 0;
    do {
        const std::size_t size = std::min(kChunkBytes, message.payload.size() - at);
        const std::uint32_t more = at + size < message.payload.size() ? 1 : 0;
        std::memcpy(packet.data(), &type, sizeof(type));
        std::memcpy(packet.data() + sizeof(type), &more, sizeof(more));
        std::memcpy(packet.data() + kHeadBytes, message.payload.data() + at, size);
        for (;;) {
            // MSG_NOSIGNAL: a host that has gone is an answer, not a SIGPIPE.
            const ssize_t sent = ::send(fd, packet.data(), kHeadBytes + size, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (sent == static_cast<ssize_t>(kHeadBytes + size)) {
                break;
            }
            if (sent < 0 && errno == EINTR) {
                continue;
            }
            // **A full socket is waited on**, briefly: the other end is
            // reading the packets before this one.
            if (sent < 0 && errno == EAGAIN && waitFor(fd, POLLOUT)) {
                continue;
            }
            return false;
        }
        at += size;
    } while (at < message.payload.size());
    return true;
}

std::optional<Message> receiveMessage(int fd, bool wait)
{
    t_wouldBlock = false;
    std::vector<char> packet(kHeadBytes + kChunkBytes);
    Message message;
    bool first = true;
    for (;;) {
        ssize_t got = 0;
        do {
            got = ::recv(fd, packet.data(), packet.size(), wait || !first ? 0 : MSG_DONTWAIT);
        } while (got < 0 && errno == EINTR);
        if (got < 0 && errno == EAGAIN) {
            if (first) {
                t_wouldBlock = true;
                return std::nullopt;
            }
            // The rest of a message that has begun: it is on its way.
            if (waitFor(fd, POLLIN)) {
                continue;
            }
            return std::nullopt;
        }
        if (got < static_cast<ssize_t>(kHeadBytes)) {
            return std::nullopt; // closed, or not one of ours
        }
        std::uint32_t type = 0;
        std::uint32_t more = 0;
        std::memcpy(&type, packet.data(), sizeof(type));
        std::memcpy(&more, packet.data() + sizeof(type), sizeof(more));
        if (!first && type != static_cast<std::uint32_t>(message.type)) {
            return std::nullopt; // the rest of a message that is not this one
        }
        message.type = static_cast<MessageType>(type);
        const auto size = static_cast<std::size_t>(got) - kHeadBytes;
        if (message.payload.size() + size > kMaxMessageBytes) {
            return std::nullopt;
        }
        message.payload.append(packet.data() + kHeadBytes, size);
        first = false;
        if (more == 0) {
            return message;
        }
    }
}

bool wouldBlock()
{
    return t_wouldBlock;
}

std::string valueText(float value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.9g", static_cast<double>(value));
    return text.data();
}

std::optional<float> parseValue(std::string_view text)
{
    float value = 0.0f;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::string pairText(std::string_view first, std::string_view second)
{
    std::string text{first};
    text += '\n';
    text += second;
    return text;
}

std::optional<std::pair<std::string, std::string>> parsePair(std::string_view payload)
{
    const std::size_t newline = payload.find('\n');
    if (newline == std::string_view::npos || newline == 0) {
        return std::nullopt;
    }
    return std::pair{std::string{payload.substr(0, newline)}, std::string{payload.substr(newline + 1)}};
}

std::string valuesText(std::string_view why, const std::vector<NamedValue> &values)
{
    std::string text{why};
    text += '\n';
    for (const NamedValue &value : values) {
        text += value.id;
        text += '=';
        text += valueText(value.value);
        text += '\n';
    }
    return text;
}

std::optional<std::pair<std::string, std::vector<NamedValue>>> parseValues(std::string_view payload)
{
    const std::size_t newline = payload.find('\n');
    if (newline == std::string_view::npos) {
        return std::nullopt;
    }
    std::pair<std::string, std::vector<NamedValue>> parsed{std::string{payload.substr(0, newline)}, {}};
    std::size_t at = newline + 1;
    while (at < payload.size()) {
        const std::size_t end = std::min(payload.find('\n', at), payload.size());
        const std::string_view line = payload.substr(at, end - at);
        at = end + 1;
        // **The last "=" divides**: an LV2 symbol has none, a CLAP id none,
        // and the value is a number either way.
        const std::size_t equals = line.rfind('=');
        if (equals == std::string_view::npos || equals == 0) {
            return std::nullopt;
        }
        const std::optional<float> value = parseValue(line.substr(equals + 1));
        if (!value) {
            return std::nullopt;
        }
        parsed.second.push_back(NamedValue{std::string{line.substr(0, equals)}, *value});
    }
    return parsed;
}

std::string presetsText(const std::vector<PresetEntry> &presets)
{
    std::string text;
    for (const PresetEntry &preset : presets) {
        // **The framing holds**: an id with a tab or a line in it could not
        // be read back as itself, so it is left out; a name's become spaces.
        if (preset.id.find_first_of("\t\n") != std::string::npos) {
            continue;
        }
        std::string name = preset.name;
        std::ranges::replace_if(name, [](char c) { return c == '\t' || c == '\n'; }, ' ');
        text += preset.id;
        text += '\t';
        text += name;
        text += '\n';
    }
    return text;
}

std::vector<PresetEntry> parsePresets(std::string_view payload)
{
    std::vector<PresetEntry> presets;
    std::size_t at = 0;
    while (at < payload.size()) {
        const std::size_t end = std::min(payload.find('\n', at), payload.size());
        const std::string_view line = payload.substr(at, end - at);
        at = end + 1;
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos || tab == 0) {
            continue;
        }
        presets.push_back(PresetEntry{std::string{line.substr(0, tab)}, std::string{line.substr(tab + 1)}});
    }
    return presets;
}

} // namespace rb::plugin
