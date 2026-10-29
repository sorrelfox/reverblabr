// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/backoff.h"

#include <algorithm>
#include <utility>

namespace rb::stream
{

Backoff::Backoff()
    : Backoff(Options{})
{
}

Backoff::Backoff(Options options)
    : m_options(std::move(options))
{
    m_state ^= static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::chrono::milliseconds Backoff::next()
{
    // Doubling stops well before it could overflow: past the longest wait
    // the answer is the longest wait.
    const std::uint32_t doublings = std::min<std::uint32_t>(m_failures, 20);
    ++m_failures;
    const std::int64_t first = std::max<std::int64_t>(m_options.first.count(), 1);
    const std::int64_t wait = std::min<std::int64_t>(first << doublings, m_options.longest.count());

    double random = 0.0;
    if (m_options.random) {
        random = std::clamp(m_options.random(), 0.0, 1.0);
    } else {
        // xorshift: there is no need for anything better to spread a few
        // reconnections out.
        m_state ^= m_state << 13;
        m_state ^= m_state >> 7;
        m_state ^= m_state << 17;
        random = static_cast<double>(m_state >> 11) / static_cast<double>(1ULL << 53);
    }
    const auto shortened = static_cast<std::int64_t>(static_cast<double>(wait) * (1.0 - 0.2 * random));
    return std::chrono::milliseconds{std::max<std::int64_t>(shortened, 1)};
}

void Backoff::connectionLasted(std::chrono::milliseconds upFor)
{
    if (upFor >= m_options.lasting) {
        m_failures = 0;
    }
}

void Backoff::reset()
{
    m_failures = 0;
}

} // namespace rb::stream
