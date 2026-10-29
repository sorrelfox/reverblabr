// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "pw/pwobjects.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace rb::pw
{

/// The way round a loop, for the message the user is shown.
struct FeedbackPath {
    /// The nodes the sound would go round, in order. The first is where the
    /// proposed link would arrive and the last is where it would leave, so
    /// the link closes the ring.
    QList<std::uint32_t> nodes;
    /// The same, by the names a person would recognise.
    QStringList names;

    /// The path written out, for the error the interface shows:
    /// "Speakers → Podcast → Speakers".
    [[nodiscard]] QString describe() const;

    [[nodiscard]] bool operator==(const FeedbackPath &other) const = default;
};

/// One link, as the guard needs to see it: which nodes it joins.
struct NodeEdge {
    std::uint32_t from = 0;
    std::uint32_t to = 0;

    [[nodiscard]] bool operator==(const NodeEdge &other) const = default;
};

/// Whether a link from \a outputNode to \a inputNode would close a loop, and
/// if so, the way round it.
///
/// Sound fed back into its own source runs away: each trip round the ring
/// adds the loop's gain, and within a second or two it is as loud as the
/// hardware allows. PipeWire will happily make such a link; nothing in it
/// looks for the ring. So every link is asked about here first, and one that
/// would close a ring is refused and reported rather than made.
///
/// **A node passes what arrives at it on to what leaves it.** That is why
/// this works on nodes rather than on ports: a sink's monitor is the same
/// node as the sink, so capturing the monitor of a sink we are playing to is
/// a ring of two.
///
/// **Nodes that share a `node.link-group` pass sound between themselves
/// without a link.** A loopback, a filter chain, a combined sink and an echo
/// canceller are each two nodes joined inside PipeWire, and a ring through
/// one of them is invisible in the links alone. They are followed here.
///
/// \a ignore leaves one link out of the walk, which is how an existing link
/// is asked whether it is the one closing a ring that has appeared around it.
[[nodiscard]] std::optional<FeedbackPath> findFeedbackPath(std::uint32_t outputNode,
                                                           std::uint32_t inputNode,
                                                           const QList<PwNode> &nodes,
                                                           const QList<PwLink> &links,
                                                           std::optional<NodeEdge> ignore = {});

} // namespace rb::pw
