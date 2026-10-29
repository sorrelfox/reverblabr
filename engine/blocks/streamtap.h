// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"

#include <cstdint>

namespace rb::rt
{
class AudioTap;
} // namespace rb::rt

namespace rb::blocks
{

/// **The audio thread's half of every block that streams** (Broadcast,
/// Live Stream): the sound passed on sample for sample, and copied into
/// \a tap with the session's rate, every cycle, pauses and all. A stream is
/// a station on the air: its audience hears time pass, and a source that
/// has stopped playing is silence rather than a gap the stream closes up,
/// as a recording does. \a tap may be null, when nothing streams.
void passOnAndTap(dsp::ProcessContext &context, rt::AudioTap *tap, std::uint32_t sampleRate) noexcept RB_NONBLOCKING;

} // namespace rb::blocks
