// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/meter.h"

#include <QString>

#include <array>
#include <cstdint>
#include <map>
#include <optional>

namespace rb::core
{

/// **Who is wanted in a session's scope slots, and who has one.** The
/// sampler's bookkeeping, and the mock's, apart so that a test can run it with
/// no audio.
///
/// A surface asks for a block by renewing a lease, and never gives one back:
/// the lease runs out. So a window that crashes lets go exactly as one that
/// closes does, a second window has nothing to agree with the first, and the
/// daemon keeps no list of who is looking.
///
/// A lease says a block is wanted. A slot is given only to a block the session
/// has, so a block asked for that is not there uses up nothing.
class ScopeClaims
{
public:
    /// Whether anything at all is going on: **all a session that draws no
    /// sound ever asks.**
    [[nodiscard]] bool isIdle() const noexcept
    {
        return m_leases.empty() && m_given.empty();
    }

    /// Wants \a blockId until \a untilNanos.
    void renew(const QString &blockId, std::uint64_t untilNanos);

    /// Forgets the leases that have run out at \a nowNanos. A block with a
    /// slot keeps it until letGo().
    void expire(std::uint64_t nowNanos);

    [[nodiscard]] bool isWanted(const QString &blockId) const;

    /// Every block wanted, whether it has a slot or not.
    template <typename Visit>
    void forEachWanted(Visit visit) const
    {
        for (const auto &[blockId, until] : m_leases) {
            visit(blockId);
        }
    }

    /// The slot \a blockId holds, or -1.
    [[nodiscard]] int slotOf(const QString &blockId) const;

    /// The meter that \a blockId's slot was given to. Compared, and
    /// dereferenced only by somebody who has seen it is still there.
    [[nodiscard]] rt::BlockMeter *meterOf(const QString &blockId) const;

    /// Gives \a blockId a free slot and returns it, or -1 when none is left.
    /// \a meter may be null where nothing real writes.
    [[nodiscard]] int give(const QString &blockId, rt::BlockMeter *meter);

    /// Frees the slot of \a blockId. Whoever writes to it must have stopped.
    void letGo(const QString &blockId);

    /// Every block with a slot.
    template <typename Visit>
    void forEachGiven(Visit visit) const
    {
        for (const auto &[blockId, given] : m_given) {
            visit(blockId, given.slot, given.meter);
        }
    }

    /// Lets go of everything, leases as well.
    void clear();

private:
    struct Given {
        int slot = -1;
        rt::BlockMeter *meter = nullptr;
    };

    std::map<QString, std::uint64_t> m_leases;
    std::map<QString, Given> m_given;
};

} // namespace rb::core
