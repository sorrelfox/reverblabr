// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/telemetry.h"

#include <QLatin1StringView>
#include <QString>

#include <cstdint>

namespace rb::ui
{

/// **Whether what a block last published can still be believed.**
///
/// A surface that says something about the sound now must not go on saying
/// it when nothing is listening, since a frozen "Quiet" reads like a true one.
/// Three things stop a block being heard, and all of them are told apart
/// here, for every surface alike:
///
/// - **The session stopped:** it publishes no blocks.
/// - **The session is overloaded:** it runs no blocks and frames still come,
///   so only the snapshot's own word shows it.
/// - **Nobody publishes:** an engine that died leaves its last snapshot in
///   memory the window has mapped, still saying it runs. The frame stops
///   moving, and nothing else does.
class TelemetryWatch
{
public:
    /// How long a frame may stand still before the snapshot is not believed.
    /// Thirty times the sampler's period, so a busy engine thread is no alarm.
    static constexpr qint64 kUnmovedMs = 500;

    /// What can be said about a block, in the words QML reads.
    static constexpr QLatin1StringView kWatching{"watching"};
    static constexpr QLatin1StringView kStopped{"stopped"};
    static constexpr QLatin1StringView kOverloaded{"overloaded"};
    static constexpr QLatin1StringView kNoNews{"noNews"};

    /// Says a read found \a snapshot at \a nowMs. Call it on every read,
    /// whether or not the snapshot is new.
    void saw(const core::TelemetrySnapshot &snapshot, qint64 nowMs) noexcept;

    /// Forgets what it saw: another session, or none.
    void forget() noexcept;

    /// Whether the frame has stood still too long at \a nowMs.
    [[nodiscard]] bool isUnmoved(qint64 nowMs) const noexcept;

    /// What can be said at \a nowMs of a block that \a published or did not
    /// appear in the last snapshot: one of the words above.
    [[nodiscard]] QLatin1StringView stateOf(bool published, qint64 nowMs) const noexcept;

    /// \a blockId in \a snapshot, or null.
    [[nodiscard]] static const core::BlockTelemetry *find(const core::TelemetrySnapshot &snapshot,
                                                          const QString &blockId);

private:
    std::uint64_t m_frame = 0;
    qint64 m_movedAt = 0;
    bool m_seen = false;
    bool m_running = false;
    bool m_shedding = false;
};

} // namespace rb::ui
