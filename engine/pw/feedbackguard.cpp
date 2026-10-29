// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/feedbackguard.h"

#include <QHash>
#include <QQueue>
#include <QSet>

using namespace Qt::StringLiterals;

namespace rb::pw
{

QString FeedbackPath::describe() const
{
    return names.join(u" → "_s);
}

std::optional<FeedbackPath> findFeedbackPath(std::uint32_t outputNode,
                                             std::uint32_t inputNode,
                                             const QList<PwNode> &nodes,
                                             const QList<PwLink> &links,
                                             std::optional<NodeEdge> ignore)
{
    QHash<std::uint32_t, QString> nameOf;
    QHash<QString, QList<std::uint32_t>> byLinkGroup;
    for (const PwNode &node : nodes) {
        nameOf.insert(node.object.id, node.description.isEmpty() ? node.name : node.description);
        if (!node.linkGroup.isEmpty()) {
            byLinkGroup[node.linkGroup].append(node.object.id);
        }
    }

    QHash<std::uint32_t, QList<std::uint32_t>> next;
    for (const PwLink &link : links) {
        if (ignore.has_value() && link.outputNode == ignore->from && link.inputNode == ignore->to) {
            continue;
        }
        next[link.outputNode].append(link.inputNode);
    }
    // A loopback, a filter chain, a combined sink: two nodes that pass sound
    // between themselves inside PipeWire, with no link to show for it.
    for (const auto &[group, members] : byLinkGroup.asKeyValueRange()) {
        for (const std::uint32_t from : members) {
            for (const std::uint32_t to : members) {
                if (from != to) {
                    next[from].append(to);
                }
            }
        }
    }

    // The proposed link leaves outputNode and arrives at inputNode, so it
    // closes a ring exactly when the sound arriving there can already find
    // its way back out of outputNode.
    if (outputNode == inputNode) {
        FeedbackPath path;
        path.nodes = {inputNode};
        path.names = {nameOf.value(inputNode, u"?"_s)};
        return path;
    }

    QHash<std::uint32_t, std::uint32_t> cameFrom;
    QSet<std::uint32_t> seen{inputNode};
    QQueue<std::uint32_t> queue;
    queue.enqueue(inputNode);

    while (!queue.isEmpty()) {
        const std::uint32_t node = queue.dequeue();
        if (node == outputNode) {
            FeedbackPath path;
            for (std::uint32_t step = outputNode;; step = cameFrom.value(step)) {
                path.nodes.prepend(step);
                path.names.prepend(nameOf.value(step, u"?"_s));
                if (step == inputNode) {
                    break;
                }
            }
            return path;
        }
        for (const std::uint32_t following : next.value(node)) {
            if (seen.contains(following)) {
                continue;
            }
            seen.insert(following);
            cameFrom.insert(following, node);
            queue.enqueue(following);
        }
    }
    return std::nullopt;
}

} // namespace rb::pw
