// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/pwobjects.h"

using namespace Qt::StringLiterals;

namespace rb::pw
{

QString portDirectionName(PortDirection direction)
{
    return direction == PortDirection::Input ? u"input"_s : u"output"_s;
}

QString nodeKindName(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Other:
        return u"other"_s;
    case NodeKind::Sink:
        return u"sink"_s;
    case NodeKind::Source:
        return u"source"_s;
    case NodeKind::PlaybackStream:
        return u"playbackStream"_s;
    case NodeKind::RecordStream:
        return u"recordStream"_s;
    }
    return u"other"_s;
}

NodeKind nodeKindFromMediaClass(QStringView mediaClass)
{
    if (mediaClass == u"Audio/Sink") {
        return NodeKind::Sink;
    }
    if (mediaClass == u"Audio/Source" || mediaClass == u"Audio/Source/Virtual") {
        return NodeKind::Source;
    }
    if (mediaClass == u"Stream/Output/Audio") {
        return NodeKind::PlaybackStream;
    }
    if (mediaClass == u"Stream/Input/Audio") {
        return NodeKind::RecordStream;
    }
    return NodeKind::Other;
}

bool PwNode::isNotification() const
{
    return mediaRole.compare(u"Notification"_s, Qt::CaseInsensitive) == 0 || !eventId.isEmpty();
}

bool PwNode::isReplayed() const
{
    return !linkGroup.isEmpty();
}

} // namespace rb::pw
