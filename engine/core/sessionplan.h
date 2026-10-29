// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/noticecodes.h"
#include "core/session.h"
#include "dsp/channelmap.h"
#include "pw/linkmanager.h"
#include "pw/pwobjects.h"

#include <QList>
#include <QSet>
#include <QString>

#include <functional>

namespace rb::pw
{
class SessionNode;
}

namespace rb::core
{

/// What one source block is to capture, worked out from the session and from
/// the graph as it stands.
///
/// **The match is what the session holds; everything else is what is there
/// at the moment.** A block whose application is not running still has a
/// target — with no streams and a stereo layout — because its ports have to
/// exist for the application to be linked to when it starts.
struct CaptureTarget {
    QString blockId;
    pw::EndpointMatch match;
    /// What the block's input ports should carry. The **widest** layout
    /// among the streams that match, so that every channel of every one of
    /// them has a port of its own to arrive at: a link only ever joins two
    /// ports of the same channel, and the mixing is the block's ([ADR-0007]).
    dsp::ChannelLayout layout;
    /// How many streams match right now. The interface turns this into
    /// "waiting for Firefox" or "capturing 2 streams"; the engine holds no
    /// words.
    int streamCount = 0;
    /// **The microphone the application is itself recording from**, when
    /// the block is set to include it: the other side of a call. Empty when
    /// the option is off.
    ///
    /// It is not stored anywhere and it is not chosen: the application
    /// picks a microphone and we follow its own capture stream back to
    /// whatever source that is, so changing it in the application changes
    /// it here. A block that is including the input but whose application
    /// is not recording has a match that names nothing and a stereo pair of
    /// ports waiting for one.
    pw::EndpointMatch microphone;
    /// What the microphone's ports should carry. Empty when the option is
    /// off; the block is given these channels after the application's, on
    /// ports of their own ([ADR-0007]).
    dsp::ChannelLayout microphoneLayout;

    [[nodiscard]] bool operator==(const CaptureTarget &other) const = default;
};

/// The match rule an application source carries, in terms that outlive a run
/// of the application: its binary, then its Flatpak id, then the name it
/// calls itself. An empty rule matches nothing, which is what a block nobody has
/// chosen an application for should do.
///
/// \a defaultSink is what the "default" output mode means at this moment;
/// with the mode set to it and no default sink anywhere, the rule matches
/// nothing, because nothing is playing to a device that is not there.
[[nodiscard]] pw::EndpointMatch applicationMatch(const BlockInstance &block, const QString &defaultSink = {});

/// The streams of every application an Application source in \a session
/// names, wherever they are playing to, each once: what a timer set to quit
/// the captured applications when it ends asks to quit. Our own nodes are
/// never among them.
[[nodiscard]] QList<pw::PwNode> capturedApplications(const Session &session, const QList<pw::PwNode> &nodes);

/// What every source block in \a session should capture, given the graph as
/// it stands: the applications an Application source names, and the device
/// an Input Device names.
///
/// Worked out from nothing but its arguments, so it can be checked against a
/// graph written out by hand. \a links and \a defaults are what decide where
/// a stream is going, which is what the output modes are about.
[[nodiscard]] QList<CaptureTarget> capturePlan(const Session &session,
                                               const QList<pw::PwNode> &nodes,
                                               const QList<pw::PwPort> &ports,
                                               const QList<pw::PwLink> &links,
                                               const pw::PwDefaults &defaults);

/// The links that make a plan true: from every channel of every matching
/// stream to the port of ours that carries that channel for that block.
///
/// **Extra links, never a move.** The application goes on playing to its own
/// device; capturing it adds links and changes nothing it can see.
[[nodiscard]] QList<pw::DesiredLink> captureLinks(const QList<CaptureTarget> &targets, const pw::SessionNode &node);
[[nodiscard]] QList<pw::DesiredLink>
captureLinks(const QList<CaptureTarget> &targets,
             const std::function<const pw::SessionNode *(const QString &)> &nodeOf);

/// Where one output block is sending, worked out from the session and the
/// graph.
struct OutputTarget {
    QString blockId;
    /// The device it resolved to: the `node.name` the block names, or
    /// whatever WirePlumber has made the default output. Empty when the
    /// block follows the default and there is none.
    QString deviceName;
    /// Names that device, and nothing else.
    pw::EndpointMatch match;
    /// What the device takes. A stereo pair when the device is not there,
    /// so that the block has ports for one to be linked to when it arrives.
    dsp::ChannelLayout layout;
    /// Whether the device is in the graph at the moment. **Not an error when
    /// it is not**: a headset that was unplugged is usually going to be
    /// plugged in again, so the block goes quiet and waits.
    bool devicePresent = false;

    [[nodiscard]] bool operator==(const OutputTarget &other) const = default;
};

[[nodiscard]] QList<OutputTarget> outputPlan(const Session &session,
                                             const QList<pw::PwNode> &nodes,
                                             const QList<pw::PwPort> &ports,
                                             const pw::PwDefaults &defaults);

/// From every channel a block puts out to the port of the device that takes
/// that channel.
[[nodiscard]] QList<pw::DesiredLink> outputLinks(const QList<OutputTarget> &targets, const pw::SessionNode &node);
[[nodiscard]] QList<pw::DesiredLink> outputLinks(const QList<OutputTarget> &targets,
                                                 const std::function<const pw::SessionNode *(const QString &)> &nodeOf);

/// The URLs the session's blocks ask to have opened when it starts, in the
/// order the blocks are in, with nothing repeated.
///
/// **The engine says what should be opened and never opens it.** Opening a
/// URL is the desktop's business — KIO in the interface, `xdg-open` in the
/// daemon that comes later — and the engine has no business starting
/// processes on somebody's session bus.
[[nodiscard]] QStringList urlsToOpen(const Session &session);

/// Everything the session should be linked to, both ways.
struct SessionPlan {
    QList<CaptureTarget> sources;
    QList<OutputTarget> outputs;

    [[nodiscard]] bool operator==(const SessionPlan &other) const = default;
};

[[nodiscard]] SessionPlan sessionPlan(const Session &session,
                                      const QList<pw::PwNode> &nodes,
                                      const QList<pw::PwPort> &ports,
                                      const QList<pw::PwLink> &links,
                                      const pw::PwDefaults &defaults);

/// Every link the plan asks for, in one list for the link manager.
[[nodiscard]] QList<pw::DesiredLink> sessionLinks(const SessionPlan &plan, const pw::SessionNode &node);

/// One thing a person may want to hear about, found between two plans.
struct PlanNotice {
    NoticeCode code;
    QString blockId;

    [[nodiscard]] bool operator==(const PlanNotice &other) const = default;
};

/// **What changed for the worse, or back, between two plans of one running
/// session**: a device an Input Device or Output Device uses left the
/// graph or came back, an Application source's streams all went or came
/// back.
///
/// Only a block in both plans with the **same match** counts: a device or an
/// application that the person changed, or a default that moved, is a
/// choice, not news. A capture comes back only if \a capturesLost holds the
/// block, which the caller keeps from the notices it has had; an application
/// seen for the first time is not "regained".
[[nodiscard]] QList<PlanNotice> planNotices(const Session &session,
                                            const SessionPlan &before,
                                            const SessionPlan &after,
                                            const QSet<QString> &capturesLost);

/// Which of a session's nodes a block's ports are on: a session cut around
/// its plugins is several (ADR-0014). Null for a block that has none.
using NodeOfBlock = std::function<const pw::SessionNode *(const QString &blockId)>;

/// The same, for a session in several nodes.
[[nodiscard]] QList<pw::DesiredLink> sessionLinks(const SessionPlan &plan, const NodeOfBlock &nodeOf);

} // namespace rb::core
