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
#include <utility>
#include <vector>

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
    /// Host to engine, in answer to OpenUi: the plugin's own window is
    /// showing.
    UiOpened = 6,
    /// Host to engine: the plugin's window has gone, "<why>": "closed" (a
    /// person closed it, or CloseUi did), "unavailable" (the plugin has no
    /// window this host can show, or there is no display to show it on), or
    /// "failed" (the plugin would not make it).
    UiClosed = 7,
    /// Host to engine: a person moved a control in the plugin's window,
    /// "<id>\n<value>", the value as the plugin has it. The plugin has it
    /// already; the engine takes it into the session without sending it
    /// back.
    ParamChanged = 8,
    /// Host to engine: a person took hold of a control in the plugin's
    /// window, or let go of it: "<id>\nbegin" or "<id>\nend". What comes
    /// between is one edit to undo, however long it took.
    ParamGesture = 9,
    /// Host to engine: the plugin's values changed together, "<why>\n"
    /// then "<id>=<value>\n" for each: "edit" when a person did it (a
    /// preset chosen), one edit to undo; "follow" when they are what
    /// something the engine asked for led to (a state put back).
    Values = 10,
    /// Host to engine, in answer to ListPresets: the plugin's presets, one
    /// "<id>\t<name>\n" each.
    Presets = 11,
    /// Host to engine: the preset the plugin's settings are now, "<id>",
    /// or nothing when they are no preset's.
    PresetShown = 12,
    /// Host to engine, in answer to SavePreset: "<id>" of the preset
    /// written, or nothing when it could not be.
    PresetSaved = 13,
    /// Engine to host: send State.
    GetState = 16,
    /// Engine to host: put this state back.
    SetState = 17,
    /// Engine to host: finish and exit.
    Quit = 18,
    /// Host to engine: a message asked for could not be sent, "<type>": a
    /// list of presets or a state larger than kMaxMessageBytes.
    Refused = 14,
    /// Engine to host: show the plugin's own window. Answered by UiOpened,
    /// or UiClosed when it cannot be shown.
    OpenUi = 19,
    /// Engine to host: close it. Answered by UiClosed.
    CloseUi = 20,
    /// Engine to host: send Presets.
    ListPresets = 21,
    /// Engine to host: put the plugin's preset "<id>" in. Its values come
    /// back as Values ("edit"), then PresetShown.
    LoadPreset = 22,
    /// Engine to host: keep the plugin's settings as its own preset called
    /// "<name>", where other hosts of its format find them. Answered by
    /// PresetSaved.
    SavePreset = 23,
};

struct Message {
    MessageType type = MessageType::Quit;
    std::string payload;
};

/// The longest message either side sends, which a plugin's state has to fit
/// in. Generous: most plugins' state is a few hundred bytes.
inline constexpr std::size_t kMaxMessageBytes = 1 << 20;

/// Asks for a socket's buffers to take a whole message, as far as the
/// system lets this process. Both ends, before use.
void widenSocket(int fd);

/// Sends \a message whole, or returns false. **A message of any size up to
/// kMaxMessageBytes**, sent as packets small enough for any socket buffer
/// (a SOCK_SEQPACKET socket refuses one larger than its buffer), which
/// receiveMessage() puts together again; a socket that is full is waited
/// on for a moment, packet by packet.
[[nodiscard]] bool sendMessage(int fd, const Message &message);

/// Receives one message, or nothing when the other end has gone (or sent
/// something that is not a message). \a wait says whether to block.
[[nodiscard]] std::optional<Message> receiveMessage(int fd, bool wait);

/// **A plugin's value in a message**: enough digits that the float read
/// back is the float written.
[[nodiscard]] std::string valueText(float value);
[[nodiscard]] std::optional<float> parseValue(std::string_view text);

/// One of a plugin's values, by the id the engine knows the setting by.
struct NamedValue {
    std::string id;
    float value = 0.0f;

    [[nodiscard]] bool operator==(const NamedValue &other) const = default;
};

/// The two lines of a ParamChanged or a ParamGesture, and back.
[[nodiscard]] std::string pairText(std::string_view first, std::string_view second);
[[nodiscard]] std::optional<std::pair<std::string, std::string>> parsePair(std::string_view payload);

/// A Values message's payload, and back: why, then the values.
[[nodiscard]] std::string valuesText(std::string_view why, const std::vector<NamedValue> &values);
[[nodiscard]] std::optional<std::pair<std::string, std::vector<NamedValue>>> parseValues(std::string_view payload);

/// A preset as Presets lists it.
struct PresetEntry {
    std::string id;
    std::string name;

    [[nodiscard]] bool operator==(const PresetEntry &other) const = default;
};

/// A preset whose id holds a tab or a new line is left out; a name's are
/// made spaces.
[[nodiscard]] std::string presetsText(const std::vector<PresetEntry> &presets);
[[nodiscard]] std::vector<PresetEntry> parsePresets(std::string_view payload);

/// Whether the socket is merely empty (true) rather than closed, after a
/// receiveMessage() that returned nothing without waiting.
[[nodiscard]] bool wouldBlock();

} // namespace rb::plugin
