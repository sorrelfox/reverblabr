// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A deliberate real-time violation, built only in the rtsan preset. Allocating
// inside a function marked RB_NONBLOCKING must stop the program with a
// RealtimeSanitizer report; the CTest case expects exactly that report.

#include "rt/realtime.h"

#include <memory>

namespace
{

int *allocateOnTheAudioThread() RB_NONBLOCKING
{
    return new int(42); // the violation: heap allocation in real-time code
}

} // namespace

int main()
{
    const std::unique_ptr<int> value(allocateOnTheAudioThread());
    return *value == 42 ? 0 : 1;
}
