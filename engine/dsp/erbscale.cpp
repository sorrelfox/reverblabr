// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/erbscale.h"

#include <cmath>

namespace rb::dsp
{

double erbHz(double hz)
{
    return 24.7 * (4.37 * hz / 1000.0 + 1.0);
}

double erbNumber(double hz)
{
    return 21.4 * std::log10(4.37 * hz / 1000.0 + 1.0);
}

double hzAtErbNumber(double erbs)
{
    return (std::pow(10.0, erbs / 21.4) - 1.0) * 1000.0 / 4.37;
}

std::vector<double> erbCentres(std::size_t count)
{
    std::vector<double> centres(count);
    const double low = erbNumber(kErbLowestHz);
    const double high = erbNumber(kErbHighestHz);
    for (std::size_t index = 0; index < count; ++index) {
        const double at = count == 1 ? 0.5 : static_cast<double>(index) / static_cast<double>(count - 1);
        centres[index] = hzAtErbNumber(low + at * (high - low));
    }
    return centres;
}

double erbSpacing(std::size_t count)
{
    if (count < 2) {
        return erbNumber(kErbHighestHz) - erbNumber(kErbLowestHz);
    }
    return (erbNumber(kErbHighestHz) - erbNumber(kErbLowestHz)) / static_cast<double>(count - 1);
}

} // namespace rb::dsp
