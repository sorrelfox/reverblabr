// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace rb::stream
{

/// **How long a stream waits before it tries the server again.**
///
/// Twice as long after each failure, from a second up to a minute, so a
/// server that restarted is back on the air within a second or two and one
/// that is gone for the evening is asked once a minute rather than hammered.
/// Each wait is shortened by up to a fifth at random, so that the sources
/// of a server that restarted do not all come back in the same instant.
///
/// **A connection that lasted starts the count again**; one that the
/// server dropped as soon as it was made does not, or a server that takes
/// the password and closes would be asked every second for ever.
class Backoff
{
public:
    struct Options {
        std::chrono::milliseconds first{1000};
        std::chrono::milliseconds longest{60000};
        /// How long a connection has to have lasted to count as one that
        /// worked.
        std::chrono::milliseconds lasting{10000};
        /// A number from 0 to 1 for the random part. Empty is the
        /// standard library's; a test gives a fixed one.
        std::function<double()> random;
    };

    Backoff();
    explicit Backoff(Options options);

    /// How long to wait after a failure, and the count of failures goes up.
    [[nodiscard]] std::chrono::milliseconds next();

    /// A connection was made and lasted \a upFor before it was lost: the
    /// count starts again if that was long enough.
    void connectionLasted(std::chrono::milliseconds upFor);

    /// Starts the count again, as when the settings change.
    void reset();

    /// Failures since the count last started.
    [[nodiscard]] std::uint32_t failures() const noexcept
    {
        return m_failures;
    }

private:
    Options m_options;
    std::uint32_t m_failures = 0;
    std::uint64_t m_state = 0x9e3779b97f4a7c15ULL;
};

} // namespace rb::stream
