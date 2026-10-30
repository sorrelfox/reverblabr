// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QUuid>

#include <cstdint>

namespace rb::core
{

/// The settings that belong to a whole session rather than to one block.
struct SessionSettings {
    /// Whether the session connects its sources and outputs by itself.
    bool autoConnect = true;
    /// "auto", or a rate in hertz written out, for example "48000". A string
    /// so that "auto" and a rate are the same field in the file.
    QString sampleRate = QStringLiteral("auto");
    /// "default", "low" or "high". A string for the same reason.
    QString latency = QStringLiteral("default");
    /// "off", or "login": the background engine starts the session the
    /// first time it starts in a login, and again after a crash (ADR-0023),
    /// which with Auto Run on is at login itself; or every time it starts,
    /// if the person chose that (autoRunStart). A string,
    /// so that a later choice is a new value rather than a new field.
    QString autoRun = QStringLiteral("off");

    [[nodiscard]] bool operator==(const SessionSettings &other) const = default;
};

/// The values of SessionSettings::autoRun.
namespace autoRun
{
inline constexpr auto off = "off";
inline constexpr auto login = "login";
} // namespace autoRun

/// **When the background engine runs the Auto Run sessions** (ADR-0023):
/// one choice for every session, in the application's settings file
/// (`reverblabrrc`, a KConfig file), where the window writes it and the
/// daemon reads it as it starts. Words in the file, not numbers.
namespace autoRunStart
{
inline constexpr auto group = "AutoRun";
inline constexpr auto key = "Start";
/// The first engine of a login, and one after a crash: a session the person
/// stopped stays stopped until the next login. The default.
inline constexpr auto onceALogin = "once-a-login";
/// Every engine that starts, whatever started it.
inline constexpr auto everyEngineStart = "every-engine-start";
} // namespace autoRunStart

/// One block on the canvas.
///
/// \a params is kept as JSON rather than as parsed parameter values on
/// purpose: a session written by a later version may name a block type this
/// one does not have, and its parameters have to survive being loaded and
/// saved again untouched. The graph compiler turns them into values when it
/// knows which block they belong to.
struct BlockInstance {
    /// Unique within the session, for example "b1". Wires name it.
    QString id;
    /// A block type id from the registry, for example "effect.volume".
    QString type;
    /// What the user called it, or empty for the block's own name.
    QString nickname;
    bool enabled = true;
    /// Where the tile sits on the canvas.
    QPointF position;
    QJsonObject params;
    /// The preset this block was last set from, or empty.
    QString preset;
    /// Set when the session was loaded and no block of this type is
    /// registered. The block is kept exactly as it was and flagged, never
    /// dropped, so that opening and saving a session cannot lose anything.
    bool unknownType = false;

    [[nodiscard]] bool operator==(const BlockInstance &other) const = default;
};

/// One connection between two blocks, from an output port to an input port.
struct Wire {
    QString fromBlock;
    std::uint32_t fromPort = 0;
    QString toBlock;
    std::uint32_t toPort = 0;

    [[nodiscard]] bool operator==(const Wire &other) const = default;
};

/// A session: the blocks, the wires between them and the settings.
///
/// This is the model the interface edits and the file holds. It changes only
/// through the edit commands in core/editcommand.h, which is what makes undo
/// and the facade's ApplyEdit possible.
struct Session {
    QUuid id = QUuid::createUuid();
    QString name;
    QString notes;
    SessionSettings settings;
    QList<BlockInstance> blocks;
    QList<Wire> wires;

    /// Counted up by every applied edit, so a listener can tell which changes
    /// it has already seen. Not saved: it means nothing between two runs.
    std::uint64_t revision = 0;

    /// Kept so that a later version's timers and automations survive a load
    /// and a save by this one.
    QJsonArray timers;
    QJsonArray automations;

    [[nodiscard]] bool operator==(const Session &other) const = default;

    [[nodiscard]] const BlockInstance *findBlock(QStringView id) const;
    [[nodiscard]] BlockInstance *findBlock(QStringView id);
    [[nodiscard]] bool hasBlock(QStringView id) const;
    [[nodiscard]] bool hasWire(const Wire &wire) const;

    /// A block id no block in this session uses, of the form "b<n>".
    [[nodiscard]] QString nextBlockId() const;

    /// Every wire that touches \a blockId, in the order they are held.
    [[nodiscard]] QList<Wire> wiresTouching(QStringView blockId) const;

    /// Whether the signal can get from \a fromBlock to \a toBlock by
    /// following wires forwards. The same block counts as reaching itself.
    ///
    /// **What makes a cycle visible before it is made**: a wire back into
    /// anything upstream of its source is a loop the graph compiler would
    /// refuse later, at the moment somebody pressed Start, with the wire
    /// already drawn and nothing saying which one is the wrong one.
    [[nodiscard]] bool reaches(QStringView fromBlock, QStringView toBlock) const;
};

} // namespace rb::core
