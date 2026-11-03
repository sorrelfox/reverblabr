// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/scopeclaims.h"

namespace rb::core
{

void ScopeClaims::renew(const QString &blockId, std::uint64_t untilNanos)
{
    m_leases[blockId] = untilNanos;
}

void ScopeClaims::expire(std::uint64_t nowNanos)
{
    std::erase_if(m_leases, [nowNanos](const auto &lease) { return lease.second <= nowNanos; });
}

bool ScopeClaims::isWanted(const QString &blockId) const
{
    return m_leases.contains(blockId);
}

int ScopeClaims::slotOf(const QString &blockId) const
{
    const auto found = m_given.find(blockId);
    return found == m_given.end() ? -1 : found->second.slot;
}

rt::BlockMeter *ScopeClaims::meterOf(const QString &blockId) const
{
    const auto found = m_given.find(blockId);
    return found == m_given.end() ? nullptr : found->second.meter;
}

int ScopeClaims::give(const QString &blockId, rt::BlockMeter *meter)
{
    std::array<bool, rt::kScopeSlots> taken{};
    for (const auto &[holder, given] : m_given) {
        taken[static_cast<std::size_t>(given.slot)] = true;
    }
    for (std::size_t slot = 0; slot < taken.size(); ++slot) {
        if (!taken[slot]) {
            m_given[blockId] = Given{static_cast<int>(slot), meter};
            return static_cast<int>(slot);
        }
    }
    return -1;
}

void ScopeClaims::letGo(const QString &blockId)
{
    m_given.erase(blockId);
}

void ScopeClaims::clear()
{
    m_leases.clear();
    m_given.clear();
}

} // namespace rb::core
