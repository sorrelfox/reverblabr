// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rb::plugin
{

/// **What an engine and a plugin host say over their control socket**
/// (ADR-0014): everything that is not a number on its way to the audio
/// thread. A private socketpair of SOCK_SEQPACKET, so one send is one
/// message and nothing needs framing.
enum class MessageType : std::uint32_t {
    /// Host to engine, once its node is in the graph: "<node.name>\n<latency>".
    Ready = 1,
    /// Host to engine, when the plugin's latency changes: "<samples>".
    Latency = 2,
    /// Host to engine, in answer to GetState: the plugin's state as text.
    State = 3,
    /// Host to engine: something went wrong, "<code>" (for example
    /// "UnknownPlugin"); the host exits after it.
    Error = 4,
    /// Host to engine, in answer to SetState, once the plugin with the state
    /// in it is the one running: "ok", or "refused" when the state could not
    /// be put in and the plugin before it is still running. What the engine
    /// held back while it loaded goes after this.
    StateLoaded = 5,
    /// Engine to host: send State.
    GetState = 16,
    /// Engine to host: put this state back.
    SetState = 17,
    /// Engine to host: finish and exit.
    Quit = 18,
};

struct Message {
    MessageType type = MessageType::Quit;
    std::string payload;
};

/// The longest message either side sends, which a plugin's state has to fit
/// in. Generous: most plugins' state is a few hundred bytes.
inline constexpr std::size_t kMaxMessageBytes = 1 << 20;

/// Sends \a message whole, or returns false.
[[nodiscard]] bool sendMessage(int fd, const Message &message);

/// Receives one message, or nothing when the other end has gone (or sent
/// something that is not a message). \a wait says whether to block.
[[nodiscard]] std::optional<Message> receiveMessage(int fd, bool wait);

/// Whether the socket is merely empty (true) rather than closed, after a
/// receiveMessage() that returned nothing without waiting.
[[nodiscard]] bool wouldBlock();

} // namespace rb::plugin
