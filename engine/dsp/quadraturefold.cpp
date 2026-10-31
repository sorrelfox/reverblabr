// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/quadraturefold.h"

#include <cmath>
#include <numbers>

namespace rb::dsp
{

namespace
{

constexpr QuadratureFold::Table kTable48{
    48000.0,
    {13.16, 178.56, 1607.01, 12180.82},
    {57.41, 537.07, 4733.29, 20782.01},
};

constexpr QuadratureFold::Table kTable96{
    96000.0,
    {12.01, 144.96, 1121.63, 8855.94},
    {49.95, 404.25, 3120.92, 27330.70},
};

} // namespace

const QuadratureFold::Table &QuadratureFold::tableFor(double sampleRate) noexcept
{
    return sampleRate < kHighTableFrom ? kTable48 : kTable96;
}

double QuadratureFold::coefficient(double cornerHz, double sampleRate) noexcept
{
    const double warped = std::tan(std::numbers::pi * cornerHz / sampleRate);
    return (warped - 1.0) / (warped + 1.0);
}

void QuadratureFold::prepare(double sampleRate) noexcept
{
    const Table &table = tableFor(sampleRate);
    const double scale = sampleRate / table.baseRate;
    for (std::size_t section = 0; section < kSections; ++section) {
        m_a.coefficient[section] = coefficient(table.a[section] * scale, sampleRate);
        m_b.coefficient[section] = coefficient(table.b[section] * scale, sampleRate);
    }
    reset();
}

} // namespace rb::dsp
