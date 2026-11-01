// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "telemetrywatch.h"

#include <QByteArray>

#include <algorithm>
#include <cstring>

namespace rb::ui
{

void TelemetryWatch::saw(const core::TelemetrySnapshot &snapshot, qint64 nowMs) noexcept
{
    if (!m_seen || snapshot.frame != m_frame) {
        m_frame = snapshot.frame;
        m_movedAt = nowMs;
        m_seen = true;
    }
    m_running = snapshot.running;
    m_shedding = snapshot.shedding;
}

void TelemetryWatch::forget() noexcept
{
    *this = TelemetryWatch{};
}

bool TelemetryWatch::isUnmoved(qint64 nowMs) const noexcept
{
    return m_seen && m_running && nowMs - m_movedAt >= kUnmovedMs;
}

QLatin1StringView TelemetryWatch::stateOf(bool published, qint64 nowMs) const noexcept
{
    if (!m_seen || !m_running || !published) {
        return kStopped;
    }
    if (isUnmoved(nowMs)) {
        return kNoNews;
    }
    return m_shedding ? kOverloaded : kWatching;
}

const core::BlockTelemetry *TelemetryWatch::find(const core::TelemetrySnapshot &snapshot, const QString &blockId)
{
    const QByteArray id = blockId.toUtf8().left(core::kMaxTelemetryIdBytes - 1);
    const std::uint32_t blocks = std::min<std::uint32_t>(snapshot.blockCount, core::kMaxMeteredBlocks);
    for (std::uint32_t block = 0; block < blocks; ++block) {
        if (std::strncmp(snapshot.blocks[block].id, id.constData(), core::kMaxTelemetryIdBytes) == 0) {
            return &snapshot.blocks[block];
        }
    }
    return nullptr;
}

} // namespace rb::ui
