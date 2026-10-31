// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/session.h"
#include "dsp/blockinfo.h"
#include "dsp/blockregistry.h"

#include <QString>

#include <cstdint>
#include <functional>

namespace rb::core
{

/// **What the walk has to be told about a block**, which the wires cannot
/// say. Apart from the walk because there are two answers: the running
/// blocks, which have every value as it is now (the session a runtime holds
/// goes stale while a knob is turned), and a session on its own, in a window
/// or a mock, read through the block type's defaults.
struct LimitFacts {
    std::function<dsp::GainReach(const BlockInstance &)> reach;
    /// Whether the block is one of the Hearing group.
    std::function<bool(const BlockInstance &)> inGroup;
    /// A setting that is a word; empty when the block has no such setting.
    std::function<QString(const BlockInstance &, const QString &paramId)> text;
    /// A setting that is a number, in the block's own unit. \a fallback when
    /// the block has no such setting.
    std::function<float(const BlockInstance &, const QString &paramId, float fallback)> number;
};

/// The facts as a session holds them, with what it leaves out read as the
/// block type's default. A type \a registry does not have counts as adding
/// gain. \a registry must outlive what is returned.
[[nodiscard]] LimitFacts limitFactsOf(const dsp::BlockRegistry &registry);

/// Why an Output Device needs a limit of its own.
enum class LimitCause : std::uint8_t {
    None, ///< It does not.
    OneChannel, ///< Both sides are picked onto one channel, which can reach +6 dB.
    TwoWires, ///< Two wires meet at an input, a sum nothing limits.
    Block, ///< A block that can add gain stands after the last ceiling.
};

struct LimitReason {
    LimitCause cause = LimitCause::None;
    /// The block, for LimitCause::Block.
    QString blockId;
};

/// **Why \a output needs a limit of its own, if it does.** The last thing on
/// any chain that holds a block of the Hearing group is a limit. Decided from
/// the wires and the settings that say which side a block works on, never
/// from a level or a slider a person drags, so it changes only when the
/// session's shape does. A block a session has switched off passes what comes
/// in, and a session with no block of the group has no limit at all.
[[nodiscard]] LimitReason lastLimitReason(const Session &session, const BlockInstance &output, const LimitFacts &facts);

[[nodiscard]] bool lastLimitNeeded(const Session &session, const BlockInstance &output, const LimitFacts &facts);

/// **The ceiling of that limit**: the lowest of \a output's own and every
/// Output ceiling upstream, so a lowered one is kept whatever stands after
/// it. If two ears differ the lower one binds both: the gain is linked, which
/// keeps the difference between the ears.
[[nodiscard]] float appliedLimitDb(const Session &session, const BlockInstance &output, const LimitFacts &facts);

} // namespace rb::core
