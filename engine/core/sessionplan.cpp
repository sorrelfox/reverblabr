// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/sessionplan.h"

#include "blocks/applicationsource.h"
#include "blocks/inputdevice.h"
#include "blocks/outputdevice.h"
#include "blocks/systemaudio.h"
#include "pw/sessionnode.h"

#include <QHash>
#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

/// A block's parameter as text, or an empty string. The session holds a
/// block's parameters as the JSON it was given, so this is where the match
/// rule is read from — not from the block instance, which may not exist yet
/// when a session is only being looked at.
[[nodiscard]] QString typeIdOf(std::string_view id)
{
    return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

[[nodiscard]] QString textParam(const BlockInstance &block, QLatin1StringView key)
{
    return block.params.value(key).toString();
}

/// The layout a node's ports add up to, in port order. Empty when it has no
/// audio ports, which is what a node looks like before its ports arrive.
[[nodiscard]] dsp::ChannelLayout
layoutOf(const pw::PwNode &node, const QList<pw::PwPort> &ports, pw::PortDirection direction)
{
    QList<const pw::PwPort *> mine;
    for (const pw::PwPort &port : ports) {
        if (port.nodeId == node.object.id && port.direction == direction && !port.channel.isEmpty()) {
            // A sink's monitor ports are outputs and say nothing about what
            // it takes; the direction is all that has to be asked.
            mine.append(&port);
        }
    }
    std::sort(mine.begin(), mine.end(), [](const pw::PwPort *left, const pw::PwPort *right) {
        return left->index < right->index;
    });

    dsp::ChannelLayout layout;
    layout.reserve(mine.size());
    for (const pw::PwPort *port : std::as_const(mine)) {
        layout.push_back(dsp::channelPositionFromName(port->channel.toStdString()));
    }
    return layout;
}

/// The kind of node a device block's choice is, which is not always the kind
/// the block usually deals in.
///
/// **An Input Device may name a sink**, and then records what applications
/// play into it from its monitor ports; **an Output Device may name a virtual
/// microphone**, and then plays into the ports that feed it. A node of kind
/// \a other only counts when it has audio ports facing \a direction; a name
/// nothing answers to is taken as \a usual, and the plan is worked out again
/// as soon as something by that name appears.
///
/// **Two nodes may share a name for a moment** — a virtual device being
/// renamed is made again before the old one goes — and the newer one, by
/// serial, is the one meant.
[[nodiscard]] pw::NodeKind deviceKindOf(const QString &name,
                                        pw::NodeKind usual,
                                        pw::NodeKind other,
                                        pw::PortDirection direction,
                                        const QList<pw::PwNode> &nodes,
                                        const QList<pw::PwPort> &ports)
{
    const pw::PwNode *newest = nullptr;
    for (const pw::PwNode &node : nodes) {
        if (node.name != name || node.isOurs) {
            continue;
        }
        const bool fits = node.kind == usual || (node.kind == other && !layoutOf(node, ports, direction).empty());
        if (fits && (newest == nullptr || node.object.serial > newest->object.serial)) {
            newest = &node;
        }
    }
    return newest != nullptr ? newest->kind : usual;
}

/// The newest node a device match names, by serial, or null. See
/// deviceKindOf() for why there can be more than one.
[[nodiscard]] const pw::PwNode *newestMatch(const pw::EndpointMatch &match, const QList<pw::PwNode> &nodes)
{
    const pw::PwNode *newest = nullptr;
    for (const pw::PwNode &node : nodes) {
        if (match.matches(node) && (newest == nullptr || node.object.serial > newest->object.serial)) {
            newest = &node;
        }
    }
    return newest;
}

/// The sinks a block's output mode names, or an empty list when the mode is
/// not a condition at all.
[[nodiscard]] QStringList outputsFor(const BlockInstance &block, const QString &defaultSink, bool &capturesNothing)
{
    capturesNothing = false;
    const QString mode = textParam(block, "outputMode"_L1);
    const QString chosen =
        QString::fromUtf8(blocks::ApplicationSource::kOutputModeChosen.data(),
                          static_cast<qsizetype>(blocks::ApplicationSource::kOutputModeChosen.size()));
    const QString any = QString::fromUtf8(blocks::ApplicationSource::kOutputModeAny.data(),
                                          static_cast<qsizetype>(blocks::ApplicationSource::kOutputModeAny.size()));
    if (mode == any) {
        return {};
    }
    if (mode == chosen) {
        QStringList outputs;
        for (const QJsonValue &entry : block.params.value("outputs"_L1).toArray()) {
            if (const QString name = entry.toString(); !name.isEmpty()) {
                outputs.append(name);
            }
        }
        // **"These outputs" with none chosen captures nothing**, rather than
        // everything: an empty list of conditions is not the absence of one.
        capturesNothing = outputs.isEmpty();
        return outputs;
    }
    // The default mode, which is also what a session written before the
    // modes existed means.
    capturesNothing = defaultSink.isEmpty();
    return capturesNothing ? QStringList{} : QStringList{defaultSink};
}

/// The application a block names, in terms that outlive a run of it, and
/// nothing about what kind of node to look for. **The binary wins, then the
/// Flatpak id, then the name**: every field that is set has to match, so a
/// rule that named all three would find nothing when the application was
/// installed another way.
///
/// Empty when nobody has chosen an application. Saying "any node" would be a
/// condition, and a match with a condition matches — so a block waiting to
/// be pointed at something would capture the lot.
[[nodiscard]] pw::EndpointMatch identityMatch(const BlockInstance &block)
{
    pw::EndpointMatch match;
    match.applicationBinary = textParam(block, "binary"_L1);
    match.flatpakId = textParam(block, "flatpakId"_L1);
    match.applicationName = textParam(block, "name"_L1);
    if (match.applicationBinary.isEmpty() && match.flatpakId.isEmpty() && match.applicationName.isEmpty()) {
        return {};
    }
    if (!match.applicationBinary.isEmpty()) {
        match.flatpakId.clear();
        match.applicationName.clear();
    } else if (!match.flatpakId.isEmpty()) {
        match.applicationName.clear();
    }
    return match;
}

[[nodiscard]] const pw::PwNode *nodeById(const QList<pw::PwNode> &nodes, std::uint32_t id)
{
    for (const pw::PwNode &candidate : nodes) {
        if (candidate.object.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

/// **The microphone the application is itself recording from**, by node.name,
/// or an empty string when it is not recording from anything.
///
/// Nothing about this is stored: the application chooses a microphone and we
/// follow its own capture stream back to whatever source is feeding it, so
/// the application switching microphones switches ours with no edit at all.
/// A capture stream the policy has not linked yet is judged by the target it
/// asked for, which is the same rule playsTo() uses in the other direction.
[[nodiscard]] QString
microphoneOf(const BlockInstance &block, const QList<pw::PwNode> &nodes, const QList<pw::PwLink> &links)
{
    pw::EndpointMatch recording = identityMatch(block);
    if (recording.isEmpty()) {
        return {};
    }
    // **The application listening, not the application playing**, which is
    // the one place in the engine that wants a capture stream.
    recording.kind = pw::NodeKind::RecordStream;
    recording.excludeOurs = true;
    recording.excludeNotifications = false;
    recording.excludeReplayed = false;

    for (const pw::PwNode &node : nodes) {
        if (!recording.matches(node)) {
            continue;
        }
        for (const pw::PwLink &link : links) {
            if (link.inputNode != node.object.id) {
                continue;
            }
            if (const pw::PwNode *source = nodeById(nodes, link.outputNode);
                source != nullptr && !source->isOurs && !source->name.isEmpty()) {
                return source->name;
            }
        }
        if (const QString wanted = node.props.value(u"target.object"_s); !wanted.isEmpty()) {
            return wanted;
        }
    }
    return {};
}

/// Whether \a node is playing to one of \a names at the moment. **The same
/// rule the link manager uses**, because a plan that counted a stream the
/// manager will not link would show "capturing 1 stream" over silence.
[[nodiscard]] bool playsTo(const pw::PwNode &node,
                           const QStringList &names,
                           const QList<pw::PwNode> &nodes,
                           const QList<pw::PwLink> &links)
{
    if (names.isEmpty()) {
        return true;
    }
    bool linkedAnywhere = false;
    for (const pw::PwLink &link : links) {
        if (link.outputNode != node.object.id) {
            continue;
        }
        const pw::PwNode *target = nodeById(nodes, link.inputNode);
        if (target == nullptr) {
            continue;
        }
        if (names.contains(target->name)) {
            return true;
        }
        // A link into something of ours is us listening, not the
        // application playing somewhere.
        linkedAnywhere = linkedAnywhere || !target->isOurs;
    }
    if (linkedAnywhere) {
        return false;
    }
    const QString wanted = node.props.value(u"target.object"_s);
    return !wanted.isEmpty() && names.contains(wanted);
}

} // namespace

pw::EndpointMatch applicationMatch(const BlockInstance &block, const QString &defaultSink)
{
    pw::EndpointMatch match = identityMatch(block);
    if (match.isEmpty()) {
        return {};
    }
    // **Only what an application plays.** A recording stream is the
    // application listening, not the application playing, and capturing it
    // would put the microphone into the session by the back door.
    match.kind = pw::NodeKind::PlaybackStream;
    // The three exclusions of [ADR-0007]: never ourselves, never an alert
    // sound, and never a stream that is re-playing something the graph has
    // already, which would count the same audio twice.
    match.excludeOurs = true;
    match.excludeNotifications = true;
    match.excludeReplayed = true;

    bool capturesNothing = false;
    match.playingTo = outputsFor(block, defaultSink, capturesNothing);
    if (capturesNothing) {
        return {}; // a mode that names nowhere takes nothing
    }
    return match;
}

QList<CaptureTarget> capturePlan(const Session &session,
                                 const QList<pw::PwNode> &nodes,
                                 const QList<pw::PwPort> &ports,
                                 const QList<pw::PwLink> &links,
                                 const pw::PwDefaults &defaults)
{
    QList<CaptureTarget> plan;
    for (const BlockInstance &block : session.blocks) {
        if (block.type == typeIdOf(blocks::InputDevice::kTypeId)) {
            // **A device is named, not matched.** An application is found by
            // what it is, because its nodes come and go under new ids; a
            // device is one node with one name, and the name is what the
            // session holds.
            CaptureTarget device;
            device.blockId = block.id;
            device.layout = dsp::stereoLayout();
            const QString chosen = textParam(block, "device"_L1);
            const QString wanted = chosen.isEmpty() || chosen == typeIdOf(blocks::InputDevice::kDefaultDevice)
                ? defaults.audioSource
                : chosen;
            if (wanted.isEmpty()) {
                plan.append(device); // nothing to record from, which is not an error
                continue;
            }
            device.match.nodeName = wanted;
            // A virtual output is recorded from its monitor, which is its
            // output ports: the same direction as a source's.
            device.match.kind =
                deviceKindOf(wanted, pw::NodeKind::Source, pw::NodeKind::Sink, pw::PortDirection::Output, nodes, ports);
            // **A device that re-plays something is still a device.** The
            // exclusion is about counting one sound twice when a whole
            // application is captured; a virtual source somebody chose by
            // name is exactly what they asked for.
            device.match.excludeReplayed = false;
            if (const pw::PwNode *node = newestMatch(device.match, nodes); node != nullptr) {
                // One, meaning the device is there. An input has no streams
                // to count.
                device.streamCount = 1;
                // A virtual source's own outputs are flagged as monitor
                // ports ([ADR-0007]), so the direction is all there is to go
                // on.
                if (const dsp::ChannelLayout layout = layoutOf(*node, ports, pw::PortDirection::Output);
                    !layout.empty()) {
                    device.layout = layout;
                }
            }
            plan.append(device);
            continue;
        }
        if (block.type == typeIdOf(blocks::SystemAudio::kTypeId)) {
            CaptureTarget everything;
            everything.blockId = block.id;
            everything.layout = dsp::stereoLayout();
            // **Every playback stream, and then what is taken out of it.**
            // A sink's monitor would carry the lot with nothing removable
            // from it, and alert sounds and excluded applications are
            // exactly what has to come out ([ADR-0007]).
            everything.match.kind = pw::NodeKind::PlaybackStream;
            everything.match.everyNodeOfKind = true;
            everything.match.excludeOurs = true;
            everything.match.excludeReplayed = true;
            everything.match.excludeNotifications = !block.params.value("includeAlerts"_L1).toBool();
            for (const QJsonValue &entry : block.params.value("excluded"_L1).toArray()) {
                if (const QString rule = entry.toString(); !rule.isEmpty()) {
                    everything.match.excludedApplications.append(rule);
                }
            }

            dsp::ChannelLayout widest;
            for (const pw::PwNode &node : nodes) {
                if (!everything.match.matches(node)) {
                    continue;
                }
                ++everything.streamCount;
                if (const dsp::ChannelLayout layout = layoutOf(node, ports, pw::PortDirection::Output);
                    layout.size() > widest.size()) {
                    widest = layout;
                }
            }
            if (!widest.empty()) {
                everything.layout = widest;
            }
            plan.append(everything);
            continue;
        }
        if (block.type != typeIdOf(blocks::ApplicationSource::kTypeId)) {
            continue;
        }
        CaptureTarget target;
        target.blockId = block.id;
        target.match = applicationMatch(block, defaults.audioSink);
        // A block with no application chosen yet still publishes a stereo
        // pair of ports: something has to be there for the picker to point
        // at, and for the first stream to be linked to.
        target.layout = dsp::stereoLayout();

        // **The other side of the call.** Ports for it exist as soon as the
        // option is on, for the same reason the application's do: the
        // source has to have somewhere to be linked to when the application
        // starts recording.
        if (block.params.value("includeInput"_L1).toBool()) {
            target.microphoneLayout = dsp::stereoLayout();
            if (const QString source = microphoneOf(block, nodes, links); !source.isEmpty()) {
                target.microphone.nodeName = source;
                target.microphone.kind = pw::NodeKind::Source;
                // A device that re-plays something is still a device: this
                // is the one the application chose, whatever it is made of.
                target.microphone.excludeReplayed = false;
                for (const pw::PwNode &node : nodes) {
                    if (!target.microphone.matches(node)) {
                        continue;
                    }
                    // A virtual source's own outputs are flagged as monitor
                    // ports ([ADR-0007]), so the direction is all there is.
                    if (const dsp::ChannelLayout layout = layoutOf(node, ports, pw::PortDirection::Output);
                        !layout.empty()) {
                        target.microphoneLayout = layout;
                    }
                    break;
                }
            }
        }

        if (target.match.isEmpty()) {
            plan.append(target);
            continue;
        }

        // **The widest of the streams that are there**, and a stereo pair
        // when none is. Starting from stereo and keeping the widest would
        // mean a mono stream could never narrow it, and then a mono
        // application would have two ports and nothing to arrive at either.
        dsp::ChannelLayout widest;
        for (const pw::PwNode &node : nodes) {
            // The same two questions the link manager asks, in the same
            // order: is this the application, and is it playing where the
            // mode says. A stream that will not be linked is not one to
            // count or to take a layout from.
            if (!target.match.matches(node) || !playsTo(node, target.match.playingTo, nodes, links)) {
                continue;
            }
            ++target.streamCount;
            // A stereo stream's channels all have a port among a 5.1 set,
            // and the other way about they would not.
            if (const dsp::ChannelLayout layout = layoutOf(node, ports, pw::PortDirection::Output);
                layout.size() > widest.size()) {
                widest = layout;
            }
        }
        if (!widest.empty()) {
            target.layout = widest;
        }
        plan.append(target);
    }
    return plan;
}

QList<pw::DesiredLink> captureLinks(const QList<CaptureTarget> &targets, const pw::SessionNode &node)
{
    return captureLinks(targets, [&node](const QString &) { return &node; });
}

QList<pw::DesiredLink> captureLinks(const QList<CaptureTarget> &targets,
                                    const std::function<const pw::SessionNode *(const QString &)> &nodeOf)
{
    QList<pw::DesiredLink> links;
    const auto channelName = [](dsp::ChannelPosition channel) {
        return QString::fromUtf8(dsp::channelPositionName(channel).data(),
                                 static_cast<qsizetype>(dsp::channelPositionName(channel).size()));
    };
    const QString microphoneRole =
        QString::fromUtf8(blocks::ApplicationSource::kMicrophoneRole.data(),
                          static_cast<qsizetype>(blocks::ApplicationSource::kMicrophoneRole.size()));

    for (const CaptureTarget &target : targets) {
        const pw::SessionNode *found = nodeOf(target.blockId);
        if (found == nullptr) {
            continue;
        }
        const pw::SessionNode &node = *found;
        if (!target.match.isEmpty()) {
            for (const dsp::ChannelPosition channel : target.layout) {
                pw::DesiredLink link;
                link.from.node = target.match;
                link.from.channel = channelName(channel);
                link.to = node.endpoint(target.blockId, channel, pw::PortDirection::Input);
                links.append(link);
            }
        }
        // **The microphone stands on its own.** A block whose application is
        // not playing may still be recording, and the other side of the call
        // is worth having either way.
        if (target.microphone.isEmpty()) {
            continue;
        }
        for (const dsp::ChannelPosition channel : target.microphoneLayout) {
            pw::DesiredLink link;
            link.from.node = target.microphone;
            link.from.channel = channelName(channel);
            link.to = node.endpoint(target.blockId, channel, pw::PortDirection::Input, microphoneRole);
            links.append(link);
        }
    }
    return links;
}

QList<OutputTarget> outputPlan(const Session &session,
                               const QList<pw::PwNode> &nodes,
                               const QList<pw::PwPort> &ports,
                               const pw::PwDefaults &defaults)
{
    QList<OutputTarget> plan;
    for (const BlockInstance &block : session.blocks) {
        if (block.type != typeIdOf(blocks::OutputDevice::kTypeId)) {
            continue;
        }
        OutputTarget target;
        target.blockId = block.id;
        target.layout = dsp::stereoLayout();

        // **"Default" is not a device; it is whatever is the default now.**
        // Storing the resolved name instead would mean a session that
        // followed the default until it was saved and then stopped.
        const QString chosen = textParam(block, "device"_L1);
        target.deviceName =
            chosen.isEmpty() || chosen == typeIdOf(blocks::OutputDevice::kDefaultDevice) ? defaults.audioSink : chosen;
        if (target.deviceName.isEmpty()) {
            plan.append(target); // nothing to send to, and nothing is an error
            continue;
        }

        target.match.nodeName = target.deviceName;
        // A virtual microphone is played into through its input ports, the
        // same direction as a sink's.
        target.match.kind = deviceKindOf(
            target.deviceName, pw::NodeKind::Sink, pw::NodeKind::Source, pw::PortDirection::Input, nodes, ports);
        if (const pw::PwNode *node = newestMatch(target.match, nodes); node != nullptr) {
            target.devicePresent = true;
            if (const dsp::ChannelLayout layout = layoutOf(*node, ports, pw::PortDirection::Input); !layout.empty()) {
                target.layout = layout;
            }
        }
        plan.append(target);
    }
    return plan;
}

QList<pw::DesiredLink> outputLinks(const QList<OutputTarget> &targets, const pw::SessionNode &node)
{
    return outputLinks(targets, [&node](const QString &) { return &node; });
}

QList<pw::DesiredLink> outputLinks(const QList<OutputTarget> &targets,
                                   const std::function<const pw::SessionNode *(const QString &)> &nodeOf)
{
    QList<pw::DesiredLink> links;
    for (const OutputTarget &target : targets) {
        const pw::SessionNode *node = nodeOf(target.blockId);
        if (target.match.isEmpty() || !target.devicePresent || node == nullptr) {
            continue;
        }
        for (const dsp::ChannelPosition channel : target.layout) {
            pw::DesiredLink link;
            link.from = node->endpoint(target.blockId, channel, pw::PortDirection::Output);
            link.to.node = target.match;
            link.to.channel = QString::fromUtf8(dsp::channelPositionName(channel).data(),
                                                static_cast<qsizetype>(dsp::channelPositionName(channel).size()));
            links.append(link);
        }
    }
    return links;
}

QStringList urlsToOpen(const Session &session)
{
    QStringList urls;
    for (const BlockInstance &block : session.blocks) {
        if (block.type != typeIdOf(blocks::ApplicationSource::kTypeId)) {
            continue;
        }
        if (!block.params.value("openUrlOnStart"_L1).toBool()) {
            continue;
        }
        const QString url = textParam(block, "url"_L1);
        // **Once each.** Two blocks pointed at the same call should not open
        // two tabs, and a block with the option on and nothing typed in is
        // a block somebody has not finished setting up.
        if (!url.isEmpty() && !urls.contains(url)) {
            urls.append(url);
        }
    }
    return urls;
}

SessionPlan sessionPlan(const Session &session,
                        const QList<pw::PwNode> &nodes,
                        const QList<pw::PwPort> &ports,
                        const QList<pw::PwLink> &links,
                        const pw::PwDefaults &defaults)
{
    SessionPlan plan;
    plan.sources = capturePlan(session, nodes, ports, links, defaults);
    plan.outputs = outputPlan(session, nodes, ports, defaults);
    return plan;
}

QList<pw::DesiredLink> sessionLinks(const SessionPlan &plan, const pw::SessionNode &node)
{
    return sessionLinks(plan, [&node](const QString &) { return &node; });
}

QList<pw::DesiredLink> sessionLinks(const SessionPlan &plan, const NodeOfBlock &nodeOf)
{
    QList<pw::DesiredLink> links = captureLinks(plan.sources, nodeOf);
    links.append(outputLinks(plan.outputs, nodeOf));
    return links;
}

QList<PlanNotice> planNotices(const Session &session,
                              const SessionPlan &before,
                              const SessionPlan &after,
                              const QSet<QString> &capturesLost)
{
    QList<PlanNotice> notices;
    for (const CaptureTarget &now : after.sources) {
        const auto was = std::ranges::find(before.sources, now.blockId, &CaptureTarget::blockId);
        if (was == before.sources.end() || was->match != now.match) {
            continue;
        }
        const BlockInstance *block = session.findBlock(now.blockId);
        if (block == nullptr) {
            continue;
        }
        const bool had = was->streamCount > 0;
        const bool has = now.streamCount > 0;
        if (block->type == typeIdOf(blocks::InputDevice::kTypeId)) {
            // An Input Device's count is its device: 1 when it is there.
            if (had && !has) {
                notices.append({NoticeCode::DeviceLost, now.blockId});
            } else if (!had && has) {
                notices.append({NoticeCode::DeviceReturned, now.blockId});
            }
        } else if (block->type == typeIdOf(blocks::ApplicationSource::kTypeId)) {
            if (had && !has) {
                notices.append({NoticeCode::CaptureLost, now.blockId});
            } else if (!had && has && capturesLost.contains(now.blockId)) {
                notices.append({NoticeCode::CaptureRegained, now.blockId});
            }
        }
    }
    for (const OutputTarget &now : after.outputs) {
        const auto was = std::ranges::find(before.outputs, now.blockId, &OutputTarget::blockId);
        if (was == before.outputs.end() || was->match != now.match) {
            continue;
        }
        if (was->devicePresent && !now.devicePresent) {
            notices.append({NoticeCode::DeviceLost, now.blockId});
        } else if (!was->devicePresent && now.devicePresent) {
            notices.append({NoticeCode::DeviceReturned, now.blockId});
        }
    }
    return notices;
}

} // namespace rb::core
