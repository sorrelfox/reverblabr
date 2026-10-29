// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>

namespace rb::core
{

/// Where the connection to the audio system stands, as the
/// `AudioSystemChanged` signal spells it. The same four words
/// pw::connectionStateName uses, because they mean the same thing.
namespace audioSystemState
{
inline constexpr QLatin1StringView disconnected{"disconnected"};
inline constexpr QLatin1StringView connecting{"connecting"};
inline constexpr QLatin1StringView connected{"connected"};
/// The connection broke and the next attempt is being waited for.
inline constexpr QLatin1StringView waiting{"waiting"};
} // namespace audioSystemState

/// What the engine is running on, and whether it can reach it.
///
/// **Names and versions, not a diagnosis.** The status bar says what the
/// engine is talking to and the Info tab lists the versions, and neither can
/// do that honestly unless the engine says what it actually found. A GUI
/// that hard-codes the name is a GUI that lies the first time the engine
/// speaks to something else.
///
/// Anything the engine has not been told is empty, and the interface shows a
/// dash rather than a guess.
struct AudioSystemInfo {
    /// One of rb::core::audioSystemState.
    QString state = QString(audioSystemState::disconnected);
    /// The server, for example "PipeWire", and the version it reported.
    QString serverName;
    QString serverVersion;
    /// The session manager whose policy the graph follows ([ADR-0002]), for
    /// example "WirePlumber", and its version.
    QString sessionManagerName;
    QString sessionManagerVersion;
    /// The node driving the graph, by the name a person would read. Empty
    /// until something says which it is.
    QString driver;
    /// The rate and the quantum the graph runs at, whether or not a
    /// session of ours is running: what PipeWire's clock settings say, a
    /// forced value when one is forced. Nought until something says.
    int rate = 0;
    int quantum = 0;

    [[nodiscard]] bool isConnected() const
    {
        return state == audioSystemState::connected;
    }

    [[nodiscard]] bool operator==(const AudioSystemInfo &other) const = default;
};

/// The JSON the facade carries, as the interface XML describes it.
[[nodiscard]] QString audioSystemToJson(const AudioSystemInfo &info);
/// The other way about, for the GUI and for a test. Anything missing keeps
/// its default, so a client reading an older engine's answer still works.
[[nodiscard]] AudioSystemInfo audioSystemFromJson(const QString &json);

} // namespace rb::core
