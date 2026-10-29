// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/linkmanager.h"

#include "pw/connection.h"
#include "pw/feedbackguard.h"
#include "pw/inventory.h"
#include "pw/logging.h"
#include "pw/registrymirror.h"

#include <QTimer>
#include <pipewire/pipewire.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::pw
{

QString linkErrorName(LinkError error)
{
    switch (error) {
    case LinkError::NoOutputPort:
        return u"noOutputPort"_s;
    case LinkError::NoInputPort:
        return u"noInputPort"_s;
    case LinkError::NotConnected:
        return u"notConnected"_s;
    case LinkError::FactoryRefused:
        return u"factoryRefused"_s;
    case LinkError::FeedbackLoop:
        return u"feedbackLoop"_s;
    }
    return u"unknown"_s;
}

bool EndpointMatch::isEmpty() const
{
    // **A kind on its own is not a condition** unless the caller says it is.
    // "Any playback stream" is a thing somebody can mean — it is what System
    // Audio means — but it must be asked for, or a rule that lost its name
    // would capture the machine.
    if (everyNodeOfKind && kind.has_value()) {
        return false;
    }
    return nodeName.isEmpty() && applicationBinary.isEmpty() && applicationName.isEmpty() && flatpakId.isEmpty()
        && !kind.has_value();
}

bool EndpointMatch::matches(const PwNode &node) const
{
    if (isEmpty()) {
        return false; // a match that says nothing matches nothing
    }
    if (!excludedApplications.isEmpty() && excludedApplications.contains(applicationKey(node))) {
        return false;
    }
    if (excludeOurs && node.isOurs) {
        return false;
    }
    if (excludeNotifications && node.isNotification()) {
        return false;
    }
    if (excludeReplayed && node.isReplayed()) {
        return false;
    }
    if (kind.has_value() && node.kind != *kind) {
        return false;
    }
    if (!nodeName.isEmpty() && node.name != nodeName) {
        return false;
    }
    if (!applicationBinary.isEmpty() && node.applicationBinary != applicationBinary) {
        return false;
    }
    if (!applicationName.isEmpty() && node.applicationName != applicationName) {
        return false;
    }
    return flatpakId.isEmpty() || node.flatpakId == flatpakId;
}

namespace
{

/// The two ports a link joins. This is what the manager keys its own links
/// by, within one connection.
struct PortPair {
    std::uint32_t outputPort = 0;
    std::uint32_t inputPort = 0;

    [[nodiscard]] auto operator<=>(const PortPair &) const = default;
};

/// One link this manager made, and the proxy that keeps it.
///
/// The link lives for as long as the proxy does: it was made with
/// `object.linger = false`, so letting go of the proxy is how a link is
/// removed, and a crash removes every one of them.
struct OwnedLink {
    LinkManager::Private *owner = nullptr;
    PortPair pair;
    /// The nodes at its two ends when it was made. A port's node is gone
    /// from the mirror by the time the server says the link went with it.
    std::uint32_t outputNode = 0;
    std::uint32_t inputNode = 0;
    pw_proxy *proxy = nullptr;
    spa_hook listener{};
    bool listening = false;

    OwnedLink() = default;
    ~OwnedLink()
    {
        if (listening) {
            spa_hook_remove(&listener);
        }
        if (proxy != nullptr) {
            pw_proxy_destroy(proxy);
        }
    }

    OwnedLink(const OwnedLink &) = delete;
    OwnedLink &operator=(const OwnedLink &) = delete;
};

} // namespace

class LinkManager::Private
{
public:
    Private(LinkManager *owner, Connection &theConnection, RegistryMirror &theMirror)
        : q(owner)
        , connection(theConnection)
        , mirror(theMirror)
    {
    }

    LinkManager *q = nullptr;
    Connection &connection;
    RegistryMirror &mirror;

    QList<DesiredLink> desired;
    std::map<PortPair, std::unique_ptr<OwnedLink>> owned;

    /// Pairs the server refused, and when it is worth trying them again.
    struct Refusal {
        std::chrono::steady_clock::time_point notBefore;
        std::chrono::milliseconds delay{0};
    };
    std::map<PortPair, Refusal> refused;

    /// Links waiting to be let go. Nothing of PipeWire's is destroyed from
    /// inside a loop iteration; see the note on RegistryMirror.
    std::vector<std::unique_ptr<OwnedLink>> retired;
    bool flushScheduled = false;

    /// Links this manager has let go of, for as long as the mirror still
    /// shows them. The server announces a removal a moment after it is asked
    /// for; until then a link on its way out must not be counted, or, when it
    /// went to break a ring, the next link looked at seems to close the same
    /// ring and both halves of it go.
    std::set<PortPair> lettingGo;
    /// Pairs of nodes, output first, found closing a ring in the pass under
    /// way. Every link of ours between them is on its way out and counts for
    /// nothing, although the pass has yet to reach it; see reconcile().
    std::set<std::pair<std::uint32_t, std::uint32_t>> ringEdges;

    /// Streams the policy is moving, and since when: from the first
    /// sign of a move until they play somewhere with that place's layout.
    struct Move {
        std::chrono::steady_clock::time_point since;
        /// Its links, not ours, when it was let go of. A move lands on a
        /// link that is not one of these.
        std::set<std::uint32_t> linksBefore;
    };
    std::map<std::uint32_t, Move> moving;
    std::chrono::milliseconds layoutWait{1000};
    /// The policy's links from playback streams, by link id, to the stream.
    /// A link's ends are gone from the mirror by the time it is removed.
    std::map<std::uint32_t, std::uint32_t> policyLinks;
    /// The default output as last seen, to tell when it changes.
    QString defaultSink;

    /// Streams we were listening to whose links went while the policy moved
    /// them. A move that remakes a stream's ports while a link of ours
    /// is on them can leave the stream suspended where it lands: silent, and
    /// unable to take another layout. A stream found like that is woken by a
    /// passive link of ours on its new port, let go of as soon as it runs.
    struct Suspect {
        std::chrono::steady_clock::time_point since;
        /// The node of ours it was linked into; a wake-up link goes there.
        std::uint32_t ourNode = 0;
        /// The wake-up link, once made.
        std::unique_ptr<OwnedLink> wakeUp;
        std::chrono::steady_clock::time_point wokenAt;
    };
    std::map<std::uint32_t, Suspect> suspects;
    std::chrono::milliseconds suspendedGrace{100};
    bool letGoBeforeMoves = true;
    QTimer *suspectTimer = nullptr;
    int wakeUps = 0;

    QTimer *settleTimer = nullptr;
    QTimer *retryTimer = nullptr;
    QTimer *layoutTimer = nullptr;
    std::chrono::milliseconds settle{20};
    std::chrono::milliseconds firstRetry{200};
    std::chrono::milliseconds longestRetry{5000};

    void scheduleReconcile();
    /// Asks for a pass no later than one settle time from now. Unlike
    /// scheduleReconcile() it never pushes a pass already waiting further
    /// off, so a steady stream of events cannot put the pass off for ever.
    void scheduleReconcileSoon();
    /// A link has appeared in the graph.
    void linkAppeared(const PwLink &link);
    /// The links as the guard should count them: the mirror's, less the
    /// ones on their way out, plus the ones just made.
    [[nodiscard]] QList<PwLink> linksForTheGuard();
    void reconcile();
    void forgetEverything();
    /// Lets go of one link the server has already taken away, and asks for
    /// another pass.
    void forget(const PortPair &pair);
    void retire(std::unique_ptr<OwnedLink> link);

    /// Every pair of ports the wanted links come down to right now.
    [[nodiscard]] std::set<PortPair> resolve();
    /// Whether \a node is playing to one of \a names at the moment.
    [[nodiscard]] bool playsTo(const PwNode &node, const QStringList &names) const;
    /// The port of \a node that \a endpoint names in \a direction, by port
    /// name if it gives one and by channel otherwise, if it is there.
    [[nodiscard]] std::optional<PwPort>
    portFor(const PwNode &node, const LinkEndpoint &endpoint, PortDirection direction) const;
    [[nodiscard]] bool create(const PortPair &pair, bool passive);
    /// Asks the server for a link and listens to it; nothing when it would not.
    [[nodiscard]] std::unique_ptr<OwnedLink> makeLink(const PortPair &pair, bool passive);
    /// Keeps an eye on \a stream after our links from it went during a move.
    void suspect(std::uint32_t stream, std::uint32_t ourNode);
    /// Wakes, or stops watching, the streams in suspects.
    void checkSuspects();
    /// Whether \a nodeId is a playback stream the policy is still moving: it
    /// has not landed, and not for longer than layoutWait.
    [[nodiscard]] bool stillMoving(std::uint32_t nodeId, std::chrono::steady_clock::time_point now);
    /// The policy is about to move \a nodeId: let go of our links from it
    /// now, before its ports are remade, and hold it until it lands.
    void letGoOfMovingStream(std::uint32_t nodeId, const char *why);
    /// Whether \a nodeId plays, through a link not ours, somewhere whose
    /// channels are its own.
    [[nodiscard]] bool landed(std::uint32_t nodeId, const std::set<std::uint32_t> &ignoring = {}) const;
    /// Asks for a pass when the wait for \a nodeId runs out.
    void askAgainWhenTheWaitIsOver(std::chrono::steady_clock::time_point since);
    void linkAppearedForMoves(const PwLink &link);
    void linkGoneForMoves(std::uint32_t linkId);
    void defaultsChanged(const PwDefaults &defaults);
    /// Forgets what was known about a node that has gone. **Ids are reused**,
    /// often by the same application restarted ([ADR-0007]), so a stream's
    /// move must not be held against the next node to get its id.
    void nodeGone(std::uint32_t nodeId);
    /// The channels of a node's ports in one direction, monitors left out.
    [[nodiscard]] QStringList channelsOf(std::uint32_t nodeId, PortDirection direction) const;
    void noteRefusal(const PortPair &pair);

    /// The node a port belongs to, or nothing when the port has gone.
    [[nodiscard]] std::optional<std::uint32_t> nodeOf(std::uint32_t port) const;
    /// Whether joining these two ports would close a loop; reports it if so.
    [[nodiscard]] bool wouldFeedBack(const PortPair &pair, std::optional<NodeEdge> ignore = {});

    int feedbackRefusals = 0;
};

namespace
{

const pw_proxy_events &linkProxyEvents()
{
    static const pw_proxy_events events = [] {
        pw_proxy_events result{};
        result.version = PW_VERSION_PROXY_EVENTS;
        // The server took the link away — a port went, or it was refused
        // after the fact. Let the proxy go and let the next pass decide
        // again; it may well want the link back once the ports return.
        result.removed = [](void *data) {
            auto *link = static_cast<OwnedLink *>(data);
            link->owner->forget(link->pair);
        };
        result.error = [](void *data, int seq, int res, const char *message) {
            Q_UNUSED(seq)
            auto *link = static_cast<OwnedLink *>(data);
            qCWarning(RB_PW) << "a link was refused:" << res << (message == nullptr ? "" : message);
            link->owner->noteRefusal(link->pair);
            link->owner->scheduleReconcile();
        };
        return result;
    }();
    return events;
}

} // namespace

void LinkManager::Private::retire(std::unique_ptr<OwnedLink> link)
{
    retired.push_back(std::move(link));
    if (flushScheduled) {
        return;
    }
    flushScheduled = true;
    QMetaObject::invokeMethod(
        q,
        [this] {
            flushScheduled = false;
            retired.clear();
        },
        Qt::QueuedConnection);
}

void LinkManager::Private::forget(const PortPair &pair)
{
    if (const auto found = owned.find(pair); found != owned.end()) {
        // Taken away by the server while its stream stays: most often the
        // policy remaking the stream's ports under our link. One that
        // goes on running is let be at the next look.
        const OwnedLink &link = *found->second;
        suspect(link.outputNode, link.inputNode);
        retire(std::move(found->second));
        owned.erase(found);
    }
    scheduleReconcile();
}

void LinkManager::Private::scheduleReconcile()
{
    settleTimer->start(static_cast<int>(settle.count()));
}

void LinkManager::Private::scheduleReconcileSoon()
{
    if (!settleTimer->isActive()) {
        settleTimer->start(static_cast<int>(settle.count()));
    }
}

void LinkManager::Private::linkAppeared(const PwLink &link)
{
    // One of ours coming back through the registry changes nothing the last
    // pass did not already know.
    if (owned.contains(PortPair{link.outputPort, link.inputPort})) {
        return;
    }
    // **Somebody else's link can close a ring around ours**, with no
    // port or node changing to say so: a patchbay joining a device's monitor
    // back into what a session records. Links appear in bursts — a stream is
    // linked port by port — and all the time, so this asks for a pass without
    // putting off one already waiting.
    scheduleReconcileSoon();
}

QList<PwLink> LinkManager::Private::linksForTheGuard()
{
    QList<PwLink> links = mirror.links();
    const auto onARingEdge = [this](const PortPair &pair) {
        const std::optional<std::uint32_t> from = nodeOf(pair.outputPort);
        const std::optional<std::uint32_t> to = nodeOf(pair.inputPort);
        return from && to && ringEdges.contains({*from, *to});
    };
    // A link let go of stops counting at once, and stops being remembered
    // once the server has taken it away. So does one of ours that joins two
    // nodes found closing a ring this pass: it is going too.
    std::set<PortPair> stillShown;
    links.removeIf([&](const PwLink &link) {
        const PortPair pair{link.outputPort, link.inputPort};
        if (owned.contains(pair)) {
            return onARingEdge(pair);
        }
        if (!lettingGo.contains(pair)) {
            return false;
        }
        stillShown.insert(pair);
        return true;
    });
    lettingGo = std::move(stillShown);
    // **The links this manager has made count as well as the ones the
    // server has announced.** One pass can make a link and then be asked
    // about another before the first has come back through the registry;
    // judged by the mirror alone, the two halves of a ring asked for at once
    // are each harmless, and both get made. A link that is in both is
    // an edge the walk sees twice, which changes nothing.
    for (const auto &[made, link] : owned) {
        const std::optional<std::uint32_t> from = nodeOf(made.outputPort);
        const std::optional<std::uint32_t> to = nodeOf(made.inputPort);
        if (from && to && !ringEdges.contains({*from, *to})) {
            PwLink edge;
            edge.outputNode = *from;
            edge.outputPort = made.outputPort;
            edge.inputNode = *to;
            edge.inputPort = made.inputPort;
            links.append(edge);
        }
    }
    return links;
}

std::optional<PwPort>
LinkManager::Private::portFor(const PwNode &node, const LinkEndpoint &endpoint, PortDirection direction) const
{
    const QList<PwPort> ports = mirror.portsOf(node.object.id, direction);
    if (ports.isEmpty()) {
        return std::nullopt;
    }
    if (!endpoint.portName.isEmpty()) {
        // A node with several ports on the same channel — one of ours, with
        // a port per block — is named port by port, and nothing else will do.
        for (const PwPort &port : ports) {
            if (port.name == endpoint.portName) {
                return port;
            }
        }
        return std::nullopt;
    }
    const QString &channel = endpoint.channel;
    if (channel.isEmpty()) {
        // No channel asked for: only a node with exactly one port of that
        // direction is unambiguous, and guessing on the others would put the
        // signal somewhere the user did not choose.
        return ports.size() == 1 ? std::optional{ports.front()} : std::nullopt;
    }
    for (const PwPort &port : ports) {
        if (port.channel == channel) {
            return port;
        }
    }
    return std::nullopt;
}

bool LinkManager::Private::playsTo(const PwNode &node, const QStringList &names) const
{
    if (names.isEmpty()) {
        return true; // not a condition
    }
    bool linkedAnywhere = false;
    for (const PwLink &link : mirror.linksOf(node.object.id)) {
        if (link.outputNode != node.object.id) {
            continue; // what it takes in says nothing about where it plays
        }
        const std::optional<PwNode> target = mirror.node(link.inputNode);
        if (!target) {
            continue;
        }
        if (names.contains(target->name)) {
            return true;
        }
        // A link into something of ours is not the application playing
        // somewhere: it is us listening. It must not count as "linked".
        linkedAnywhere = linkedAnywhere || !target->isOurs;
    }
    if (linkedAnywhere) {
        return false;
    }
    // **Nothing has linked it yet.** The policy takes a moment, and in that
    // moment what the stream asked for is the best there is to go on.
    const QString wanted = node.props.value(u"target.object"_s);
    return !wanted.isEmpty() && names.contains(wanted);
}

std::set<PortPair> LinkManager::Private::resolve()
{
    std::set<PortPair> pairs;
    const QList<PwNode> nodes = mirror.nodes();

    for (const DesiredLink &wanted : desired) {
        // A match may name several nodes — every stream of an application —
        // and then the link is wanted to each of them.
        for (const PwNode &outputNode : nodes) {
            if (!wanted.from.node.matches(outputNode) || !playsTo(outputNode, wanted.from.node.playingTo)) {
                continue;
            }
            const std::optional<PwPort> outputPort = portFor(outputNode, wanted.from, PortDirection::Output);
            if (!outputPort) {
                continue; // its ports are not there yet, or not that channel
            }
            for (const PwNode &inputNode : nodes) {
                if (!wanted.to.node.matches(inputNode) || !playsTo(inputNode, wanted.to.node.playingTo)) {
                    continue;
                }
                const std::optional<PwPort> inputPort = portFor(inputNode, wanted.to, PortDirection::Input);
                if (!inputPort) {
                    continue;
                }
                pairs.insert(PortPair{outputPort->object.id, inputPort->object.id});
            }
        }
    }
    return pairs;
}

std::unique_ptr<OwnedLink> LinkManager::Private::makeLink(const PortPair &pair, bool passive)
{
    pw_core *core = connection.core();
    if (core == nullptr) {
        return nullptr;
    }

    pw_properties *props = pw_properties_new(PW_KEY_OBJECT_LINGER, "false", nullptr);
    pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", pair.outputPort);
    pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", pair.inputPort);
    if (const std::optional<PwPort> port = mirror.port(pair.outputPort); port) {
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", port->nodeId);
    }
    if (const std::optional<PwPort> port = mirror.port(pair.inputPort); port) {
        pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", port->nodeId);
    }
    if (passive) {
        pw_properties_set(props, PW_KEY_LINK_PASSIVE, "true");
    }

    auto *proxy = static_cast<pw_proxy *>(
        pw_core_create_object(core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0));
    pw_properties_free(props);
    if (proxy == nullptr) {
        return nullptr;
    }

    auto link = std::make_unique<OwnedLink>();
    link->owner = this;
    link->pair = pair;
    link->outputNode = nodeOf(pair.outputPort).value_or(0);
    link->inputNode = nodeOf(pair.inputPort).value_or(0);
    link->proxy = proxy;
    pw_proxy_add_listener(proxy, &link->listener, &linkProxyEvents(), link.get());
    link->listening = true;
    return link;
}

bool LinkManager::Private::create(const PortPair &pair, bool passive)
{
    if (connection.core() == nullptr) {
        Q_EMIT q->linkFailed(LinkError::NotConnected, u"no connection"_s);
        return false;
    }
    std::unique_ptr<OwnedLink> link = makeLink(pair, passive);
    if (!link) {
        noteRefusal(pair);
        Q_EMIT q->linkFailed(LinkError::FactoryRefused, u"%1 to %2"_s.arg(pair.outputPort).arg(pair.inputPort));
        return false;
    }
    owned.emplace(pair, std::move(link));
    refused.erase(pair);
    return true;
}

void LinkManager::Private::noteRefusal(const PortPair &pair)
{
    Refusal &refusal = refused[pair];
    refusal.delay = refusal.delay.count() == 0 ? firstRetry : std::min(longestRetry, refusal.delay * 2);
    refusal.notBefore = std::chrono::steady_clock::now() + refusal.delay;
    retryTimer->start(static_cast<int>(refusal.delay.count()));
}

std::optional<std::uint32_t> LinkManager::Private::nodeOf(std::uint32_t port) const
{
    const std::optional<PwPort> found = mirror.port(port);
    return found ? std::optional{found->nodeId} : std::nullopt;
}

bool LinkManager::Private::wouldFeedBack(const PortPair &pair, std::optional<NodeEdge> ignore)
{
    const std::optional<std::uint32_t> outputNode = nodeOf(pair.outputPort);
    const std::optional<std::uint32_t> inputNode = nodeOf(pair.inputPort);
    if (!outputNode || !inputNode) {
        return false; // a port that is not there cannot be linked anyway
    }
    const QList<PwLink> links = linksForTheGuard();
    const std::optional<FeedbackPath> path = findFeedbackPath(*outputNode, *inputNode, mirror.nodes(), links, ignore);
    if (!path) {
        return false;
    }
    ++feedbackRefusals;
    qCWarning(RB_PW) << "refusing a link that would feed back:" << path->describe();
    Q_EMIT q->feedbackRefused(*path);
    Q_EMIT q->linkFailed(LinkError::FeedbackLoop, path->describe());
    return true;
}

QStringList LinkManager::Private::channelsOf(std::uint32_t nodeId, PortDirection direction) const
{
    QStringList channels;
    for (const PwPort &port : mirror.portsOf(nodeId, direction)) {
        if (!port.isMonitor) {
            channels.append(port.channel);
        }
    }
    channels.sort();
    return channels;
}

bool LinkManager::Private::landed(std::uint32_t nodeId, const std::set<std::uint32_t> &ignoring) const
{
    const QStringList ours = channelsOf(nodeId, PortDirection::Output);
    for (const PwLink &link : mirror.linksOf(nodeId)) {
        if (link.outputNode != nodeId || ignoring.contains(link.object.id)) {
            continue;
        }
        // **Up, not merely there**: a link of ours made while the policy's
        // is still agreeing its format can make the policy's fail.
        const std::optional<PwNode> target = mirror.node(link.inputNode);
        if (link.isUp() && target && !target->isOurs && channelsOf(target->object.id, PortDirection::Input) == ours) {
            return true;
        }
    }
    return false;
}

void LinkManager::Private::askAgainWhenTheWaitIsOver(std::chrono::steady_clock::time_point since)
{
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(since + layoutWait - std::chrono::steady_clock::now());
    const int wait = static_cast<int>(std::max<std::chrono::milliseconds::rep>(1, left.count()));
    if (!layoutTimer->isActive() || layoutTimer->remainingTime() > wait) {
        layoutTimer->start(wait);
    }
}

bool LinkManager::Private::stillMoving(std::uint32_t nodeId, std::chrono::steady_clock::time_point now)
{
    const std::optional<PwNode> node = mirror.node(nodeId);
    auto known = moving.find(nodeId);
    const std::set<std::uint32_t> before =
        known == moving.end() ? std::set<std::uint32_t>{} : known->second.linksBefore;
    if (!node || node->kind != NodeKind::PlaybackStream || landed(nodeId, before)) {
        moving.erase(nodeId);
        return false;
    }
    if (known == moving.end()) {
        // Not seen moving yet. It is moving if it plays somewhere, not ours,
        // whose layout it has not been given: the policy links it first and
        // gives it the layout after.
        const bool playsSomewhere = std::ranges::any_of(mirror.linksOf(nodeId), [&](const PwLink &link) {
            const std::optional<PwNode> target = mirror.node(link.inputNode);
            return link.outputNode == nodeId && target && !target->isOurs;
        });
        if (!playsSomewhere) {
            return false;
        }
        known = moving.emplace(nodeId, Move{.since = now, .linksBefore = {}}).first;
    }
    if (now - known->second.since >= layoutWait) {
        return false; // a layout of its own, it seems; link it after all
    }
    askAgainWhenTheWaitIsOver(known->second.since);
    return true;
}

void LinkManager::Private::letGoOfMovingStream(std::uint32_t nodeId, const char *why)
{
    if (!letGoBeforeMoves) {
        return;
    }
    std::vector<PortPair> ours;
    for (const auto &[pair, link] : owned) {
        if (nodeOf(pair.outputPort) == nodeId) {
            ours.push_back(pair);
        }
    }
    if (ours.empty()) {
        return; // not a stream we listen to; the policy can move it as it likes
    }
    // **At once, not after the settling time**: the policy is on its way to
    // remaking the stream's ports.
    std::uint32_t ourNode = 0;
    for (const PortPair &pair : ours) {
        if (const auto found = owned.find(pair); found != owned.end()) {
            ourNode = found->second->inputNode;
            lettingGo.insert(pair);
            // **Destroyed now, not on the next turn**, unlike every other
            // link let go of (see retire()): a turn is long enough for the
            // policy to start remaking the ports. It is safe here because
            // this is never called from the link's own events, and PipeWire
            // keeps a destroyed proxy's id until the server has let go of it,
            // so a message for it still on its way is dropped.
            found->second.reset();
            owned.erase(found);
        }
    }
    suspect(nodeId, ourNode);
    Move move{.since = std::chrono::steady_clock::now(), .linksBefore = {}};
    for (const PwLink &link : mirror.linksOf(nodeId)) {
        const std::optional<PwNode> target = mirror.node(link.inputNode);
        if (link.outputNode == nodeId && target && !target->isOurs) {
            move.linksBefore.insert(link.object.id);
        }
    }
    askAgainWhenTheWaitIsOver(move.since);
    moving[nodeId] = std::move(move);
    qCDebug(RB_PW) << "a stream we listen to is being moved (" << why << "); let go of" << ours.size()
                   << "links from node" << nodeId;
    Q_EMIT q->reconciled(0, static_cast<int>(ours.size()));
}

void LinkManager::Private::linkAppearedForMoves(const PwLink &link)
{
    const std::optional<PwNode> from = mirror.node(link.outputNode);
    const std::optional<PwNode> to = mirror.node(link.inputNode);
    if (from && from->kind == NodeKind::PlaybackStream && to && !to->isOurs) {
        policyLinks[link.object.id] = link.outputNode;
    }
}

void LinkManager::Private::linkGoneForMoves(std::uint32_t linkId)
{
    const auto found = policyLinks.find(linkId);
    if (found == policyLinks.end()) {
        return;
    }
    const std::uint32_t stream = found->second;
    policyLinks.erase(found);
    // **The policy lets go of its own link first, then remakes the stream's
    // ports.** Whatever moved it, that is the last moment ours can go before
    // the ports do.
    letGoOfMovingStream(stream, "the policy let go of it");
}

void LinkManager::Private::defaultsChanged(const PwDefaults &defaults)
{
    const QString before = std::exchange(defaultSink, defaults.audioSink);
    if (before.isEmpty() || before == defaults.audioSink) {
        return;
    }
    // A stream that asked for no output of its own follows the default one,
    // so the policy is about to move it.
    std::set<std::uint32_t> following;
    for (const auto &[pair, link] : owned) {
        const std::optional<std::uint32_t> node = nodeOf(pair.outputPort);
        if (!node) {
            continue;
        }
        const std::optional<PwNode> stream = mirror.node(*node);
        if (stream && stream->kind == NodeKind::PlaybackStream && mirror.moveTarget(*node).isEmpty()
            && stream->props.value(u"target.object"_s).isEmpty()) {
            following.insert(*node);
        }
    }
    for (const std::uint32_t node : following) {
        letGoOfMovingStream(node, "the default output changed");
    }
}

void LinkManager::Private::nodeGone(std::uint32_t nodeId)
{
    moving.erase(nodeId);
    if (const auto found = suspects.find(nodeId); found != suspects.end()) {
        if (found->second.wakeUp) {
            retire(std::move(found->second.wakeUp));
        }
        suspects.erase(found);
    }
    std::erase_if(policyLinks, [nodeId](const auto &entry) { return entry.second == nodeId; });
}

void LinkManager::Private::suspect(std::uint32_t stream, std::uint32_t ourNode)
{
    const std::optional<PwNode> node = mirror.node(stream);
    if (!node || node->kind != NodeKind::PlaybackStream || ourNode == 0 || suspects.contains(stream)) {
        return;
    }
    Suspect watched;
    watched.since = std::chrono::steady_clock::now();
    watched.ourNode = ourNode;
    suspects.emplace(stream, std::move(watched));
    suspectTimer->start(static_cast<int>(suspendedGrace.count()));
}

void LinkManager::Private::checkSuspects()
{
    const auto now = std::chrono::steady_clock::now();
    for (auto entry = suspects.begin(); entry != suspects.end();) {
        const std::uint32_t stream = entry->first;
        Suspect &watched = entry->second;
        const std::optional<PwNode> node = mirror.node(stream);
        const bool ours =
            std::ranges::any_of(owned, [&](const auto &made) { return made.second->outputNode == stream; });
        const bool runs = node && node->state == NodeState::Running;
        const bool tooLong =
            now - watched.since > 3 * layoutWait || (watched.wakeUp && now - watched.wokenAt > std::chrono::seconds{1});
        if (!node || runs || ours || tooLong) {
            // Woken, gone, listened to again anyway, or past saving.
            if (watched.wakeUp) {
                qCDebug(RB_PW) << "woke node" << stream << (runs ? "" : "(or gave up on it)");
                retire(std::move(watched.wakeUp));
            }
            entry = suspects.erase(entry);
            continue;
        }
        if (watched.wakeUp || !landed(stream) || node->state != NodeState::Suspended
            || now - watched.since < suspendedGrace) {
            ++entry; // not yet, or already being woken
            continue;
        }
        // **Suspended where it plays**: its move remade its ports under a
        // link of ours and left it so. A passive link of ours on its new
        // port wakes it. It goes as soon as the stream runs, a few
        // milliseconds of whatever it carries at most.
        const QList<PwPort> outputs = mirror.portsOf(stream, PortDirection::Output);
        const QList<PwPort> inputs = mirror.portsOf(watched.ourNode, PortDirection::Input);
        if (outputs.isEmpty() || inputs.isEmpty()
            || findFeedbackPath(stream, watched.ourNode, mirror.nodes(), linksForTheGuard(), std::nullopt)) {
            entry = suspects.erase(entry);
            continue;
        }
        watched.wakeUp = makeLink(PortPair{outputs.front().object.id, inputs.front().object.id}, true);
        if (!watched.wakeUp) {
            entry = suspects.erase(entry);
            continue;
        }
        watched.wokenAt = now;
        ++wakeUps;
        qCDebug(RB_PW) << "node" << stream << "was left suspended by its move; waking it";
        ++entry;
    }
    if (!suspects.empty()) {
        suspectTimer->start(50);
    }
}

void LinkManager::Private::reconcile()
{
    if (!connection.isConnected()) {
        return;
    }

    const std::set<PortPair> wanted = resolve();
    const auto now = std::chrono::steady_clock::now();
    // The policy's links as they are now, in case any came before this
    // manager did or were missed.
    policyLinks.clear();
    for (const PwLink &link : mirror.links()) {
        linkAppearedForMoves(link);
    }
    int created = 0;
    int removed = 0;

    // **A ring runs through nodes, not ports**. The two channels of
    // one lane are two links between the same two nodes, and letting go of
    // one of them leaves the ring standing on the other. Which of our links
    // is looked at first follows port ids, which PipeWire reuses; were the
    // channels of the ring's two halves taken in turn, the second half would
    // still see a ring through the first half's other channel, one link of
    // each would go, and the ring would be reported twice, once from each
    // end. So once a pair of nodes is found closing a ring, every link of ours
    // between them goes with it, stops counting for the guard at once, and is
    // not asked about again this pass.
    ringEdges.clear();
    const auto closesARingAlready = [&](const PortPair &pair) {
        const std::optional<std::uint32_t> outputNode = nodeOf(pair.outputPort);
        const std::optional<std::uint32_t> inputNode = nodeOf(pair.inputPort);
        return outputNode && inputNode && ringEdges.contains({*outputNode, *inputNode});
    };

    // Let go of what is no longer wanted, and of anything whose ports have
    // gone. Only ever our own links: one we did not make is not ours to
    // remove, whoever made it and whatever it joins.
    for (auto entry = owned.begin(); entry != owned.end();) {
        const bool stillWanted = wanted.contains(entry->first);
        const bool portsStillThere =
            mirror.port(entry->first.outputPort).has_value() && mirror.port(entry->first.inputPort).has_value();
        // A ring can close around a link that was safe when it was made — the
        // default device moves, somebody else adds a link. The question is
        // asked of the graph without this link in it: if the sound can get
        // back round anyway, this link is the one closing the ring.
        bool closesALoop = false;
        if (stillWanted && portsStillThere) {
            const std::optional<std::uint32_t> outputNode = nodeOf(entry->first.outputPort);
            const std::optional<std::uint32_t> inputNode = nodeOf(entry->first.inputPort);
            if (outputNode && inputNode) {
                closesALoop = ringEdges.contains({*outputNode, *inputNode})
                    || wouldFeedBack(entry->first, NodeEdge{*outputNode, *inputNode});
                if (closesALoop) {
                    ringEdges.insert({*outputNode, *inputNode});
                }
            }
        }
        if (stillWanted && portsStillThere && !closesALoop) {
            ++entry;
            continue;
        }
        lettingGo.insert(entry->first);
        retire(std::move(entry->second));
        entry = owned.erase(entry);
        ++removed;
    }

    for (const PortPair &pair : wanted) {
        if (owned.contains(pair)) {
            continue;
        }
        // Somebody else already joins those two ports. Adding ours would
        // double the signal, because a port sums every link into it.
        const bool alreadyThere = std::ranges::any_of(mirror.links(), [&pair](const PwLink &link) {
            return link.outputPort == pair.outputPort && link.inputPort == pair.inputPort;
        });
        if (alreadyThere) {
            continue;
        }
        if (const auto refusal = refused.find(pair); refusal != refused.end() && now < refusal->second.notBefore) {
            continue; // the server said no; it is not yet time to ask again
        }
        if (closesARingAlready(pair)) {
            continue; // its other channel has just been let go of for closing a ring
        }
        if (const std::optional<std::uint32_t> from = nodeOf(pair.outputPort); from && stillMoving(*from, now)) {
            continue; // the policy is still moving it; a link now would keep its old layout
        }
        if (wouldFeedBack(pair)) {
            continue; // it would run away; the rest of the session carries on
        }
        if (create(pair, false)) {
            ++created;
        }
    }

    if (created > 0 || removed > 0) {
        qCDebug(RB_PW) << "reconciled links: made" << created << "let go" << removed;
    }
    Q_EMIT q->reconciled(created, removed);
}

void LinkManager::Private::forgetEverything()
{
    // The connection has gone, so the proxies are already worthless and the
    // server has removed the links with it.
    retired.clear();
    owned.clear();
    refused.clear();
    lettingGo.clear();
    moving.clear();
    policyLinks.clear();
    suspects.clear();
}

LinkManager::LinkManager(Connection &connection, RegistryMirror &mirror, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, connection, mirror))
{
    d->settleTimer = new QTimer(this);
    d->settleTimer->setSingleShot(true);
    connect(d->settleTimer, &QTimer::timeout, this, [this] { d->reconcile(); });

    d->retryTimer = new QTimer(this);
    d->retryTimer->setSingleShot(true);
    connect(d->retryTimer, &QTimer::timeout, this, [this] { d->scheduleReconcile(); });

    d->layoutTimer = new QTimer(this);
    d->layoutTimer->setSingleShot(true);
    connect(d->layoutTimer, &QTimer::timeout, this, [this] { d->scheduleReconcile(); });

    d->suspectTimer = new QTimer(this);
    d->suspectTimer->setSingleShot(true);
    connect(d->suspectTimer, &QTimer::timeout, this, [this] { d->checkSuspects(); });
    connect(&mirror, &RegistryMirror::linkStateChanged, this, [this](const PwLink &link) {
        // A moving stream's new place is up: it may be linked now.
        if (d->moving.contains(link.outputNode) || d->suspects.contains(link.outputNode)) {
            d->scheduleReconcile();
            d->suspectTimer->start(0);
        }
    });
    connect(&mirror, &RegistryMirror::nodeStateChanged, this, [this] {
        if (!d->suspects.empty()) {
            d->suspectTimer->start(0);
        }
    });

    // Every change to the graph asks the same question again. Ports matter
    // most: a link is made when the ports it needs appear, not when their
    // node does.
    const auto onChange = [this] { d->scheduleReconcile(); };
    connect(&mirror, &RegistryMirror::portAdded, this, onChange);
    connect(&mirror, &RegistryMirror::portRemoved, this, onChange);
    connect(&mirror, &RegistryMirror::nodeAdded, this, onChange);
    connect(&mirror, &RegistryMirror::nodeChanged, this, onChange);
    connect(&mirror, &RegistryMirror::nodeRemoved, this, [this](std::uint32_t id, std::uint64_t) {
        d->nodeGone(id);
        d->scheduleReconcile();
    });
    connect(&mirror, &RegistryMirror::linkRemoved, this, onChange);
    connect(&mirror, &RegistryMirror::linkAdded, this, [this](const PwLink &link) {
        d->linkAppearedForMoves(link);
        d->linkAppeared(link);
    });
    // **A stream the policy is about to move is let go of first**.
    connect(&mirror, &RegistryMirror::moveRequested, this, [this](std::uint32_t node) {
        d->letGoOfMovingStream(node, "asked to move");
    });
    connect(&mirror, &RegistryMirror::linkRemoved, this, [this](std::uint32_t id, std::uint64_t) {
        d->linkGoneForMoves(id);
    });
    connect(&mirror, &RegistryMirror::defaultsChanged, this, [this](const PwDefaults &defaults) {
        d->defaultsChanged(defaults);
    });
    d->defaultSink = mirror.defaults().audioSink;
    connect(&mirror, &RegistryMirror::ready, this, onChange);
    connect(&mirror, &RegistryMirror::cleared, this, [this] { d->forgetEverything(); });
}

LinkManager::~LinkManager()
{
    d->forgetEverything();
}

void LinkManager::setDesiredLinks(QList<DesiredLink> links)
{
    d->desired = std::move(links);
    d->refused.clear();
    d->scheduleReconcile();
}

QList<DesiredLink> LinkManager::desiredLinks() const
{
    return d->desired;
}

int LinkManager::ownedLinkCount() const
{
    return static_cast<int>(d->owned.size());
}

bool LinkManager::owns(std::uint32_t outputPort, std::uint32_t inputPort) const
{
    return d->owned.contains(PortPair{outputPort, inputPort});
}

void LinkManager::setSettleTime(std::chrono::milliseconds settle)
{
    d->settle = std::max(std::chrono::milliseconds{0}, settle);
}

void LinkManager::setRetryDelays(std::chrono::milliseconds first, std::chrono::milliseconds longest)
{
    d->firstRetry = std::max(std::chrono::milliseconds{1}, first);
    d->longestRetry = std::max(d->firstRetry, longest);
}

void LinkManager::setLetGoBeforeMoves(bool letGo)
{
    d->letGoBeforeMoves = letGo;
}

int LinkManager::wakeUpCount() const
{
    return d->wakeUps;
}

int LinkManager::feedbackRefusalCount() const
{
    return d->feedbackRefusals;
}

void LinkManager::setLayoutWait(std::chrono::milliseconds wait)
{
    d->layoutWait = std::max(std::chrono::milliseconds{0}, wait);
}

void LinkManager::reconcileNow()
{
    d->settleTimer->stop();
    d->reconcile();
}

} // namespace rb::pw
