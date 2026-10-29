// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/session.h"

#include <algorithm>

namespace rb::core
{

const BlockInstance *Session::findBlock(QStringView id) const
{
    const auto found = std::ranges::find(blocks, id, &BlockInstance::id);
    return found == blocks.cend() ? nullptr : &*found;
}

BlockInstance *Session::findBlock(QStringView id)
{
    const auto found = std::ranges::find(blocks, id, &BlockInstance::id);
    return found == blocks.end() ? nullptr : &*found;
}

bool Session::hasBlock(QStringView id) const
{
    return findBlock(id) != nullptr;
}

bool Session::hasWire(const Wire &wire) const
{
    return wires.contains(wire);
}

QString Session::nextBlockId() const
{
    // The lowest "b<n>" nobody is using, so that ids stay short and a session
    // that has had blocks removed does not grow them for ever.
    for (int number = 1; number <= blocks.size() + 1; ++number) {
        const QString candidate = QStringLiteral("b%1").arg(number);
        if (!hasBlock(candidate)) {
            return candidate;
        }
    }
    return QStringLiteral("b%1").arg(blocks.size() + 1);
}

bool Session::reaches(QStringView fromBlock, QStringView toBlock) const
{
    if (fromBlock == toBlock) {
        return true;
    }
    // Breadth first over the wires, with the blocks already seen kept so
    // that a session which already holds a loop — one loaded from a file
    // written by hand — is walked once rather than for ever.
    QList<QString> pending{fromBlock.toString()};
    QList<QString> seen{pending};
    while (!pending.isEmpty()) {
        const QString at = pending.takeLast();
        for (const Wire &wire : wires) {
            if (wire.fromBlock != at || seen.contains(wire.toBlock)) {
                continue;
            }
            if (wire.toBlock == toBlock) {
                return true;
            }
            seen.append(wire.toBlock);
            pending.append(wire.toBlock);
        }
    }
    return false;
}

QList<Wire> Session::wiresTouching(QStringView blockId) const
{
    QList<Wire> touching;
    for (const Wire &wire : wires) {
        if (wire.fromBlock == blockId || wire.toBlock == blockId) {
            touching.append(wire);
        }
    }
    return touching;
}

} // namespace rb::core
