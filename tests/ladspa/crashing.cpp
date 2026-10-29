// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A LADSPA library that crashes as soon as anybody asks what is in it:
// what a scan has to survive, by blocklisting it and carrying on.

#include "plugin/ladspaabi.h"

#include <cstdlib>

extern "C" __attribute__((visibility("default"))) const rb::ladspa::Descriptor *ladspa_descriptor(unsigned long)
{
    std::abort();
}
