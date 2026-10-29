// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace rb::core
{

/// One moment a session's node was late: when it was noticed, how
/// many cycles it had been late since it started, and how many of those
/// were new since the last look.
struct XrunEvent {
    /// Milliseconds since the epoch, on the wall clock: this is for a
    /// person to line up with what they were doing, not for timing.
    qint64 atMs = 0;
    std::uint32_t total = 0;
    std::uint32_t added = 0;

    [[nodiscard]] bool operator==(const XrunEvent &other) const = default;
};

/// **When a session was late, not only how often**. The node counts
/// xruns since it started; the diagnostics page wants to know whether they
/// came in a burst when something else started, or one a minute all day.
/// The telemetry sampler shows it the count sixty times a second, and each
/// time the count has gone up is one event here.
class XrunHistory
{
public:
    /// How many events are kept; the oldest go first.
    static constexpr std::size_t kCapacity = 64;

    /// The node's count now, read at \a atMs. A count lower than the last
    /// one is a node that started again, and counts from nought.
    void observe(std::uint32_t total, qint64 atMs)
    {
        if (total < m_last) {
            m_last = 0;
        }
        if (total > m_last) {
            m_events.push_back(XrunEvent{atMs, total, total - m_last});
            while (m_events.size() > kCapacity) {
                m_events.pop_front();
            }
        }
        m_last = total;
    }

    /// A new node: its count starts at nought, and the events so far stay.
    void restart()
    {
        m_last = 0;
    }

    [[nodiscard]] std::vector<XrunEvent> events() const
    {
        return {m_events.begin(), m_events.end()};
    }

private:
    std::deque<XrunEvent> m_events;
    std::uint32_t m_last = 0;
};

} // namespace rb::core
