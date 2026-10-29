// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/registrymirror.h"

#include "pw/connection.h"
#include "pw/logging.h"
#include "pw/virtualdeviceconf.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::pw
{
namespace
{

/// The metadata object WirePlumber keeps the defaults in.
constexpr const char *kDefaultMetadataName = "default";
/// The one the server keeps its clock settings in.
constexpr const char *kSettingsMetadataName = "settings";

QHash<QString, QString> allProps(const spa_dict *props)
{
    QHash<QString, QString> result;
    if (props == nullptr) {
        return result;
    }
    const spa_dict_item *item = nullptr;
    spa_dict_for_each(item, props)
    {
        if (item->key != nullptr) {
            result.insert(QString::fromUtf8(item->key),
                          item->value == nullptr ? QString{} : QString::fromUtf8(item->value));
        }
    }
    return result;
}

/// A property of the merged set, which is the registry's announcement with
/// whatever the object's own info event added on top.
QString merged(const QHash<QString, QString> &props, const char *key)
{
    return props.value(QString::fromUtf8(key));
}

qint64 mergedNumber(const QHash<QString, QString> &props, const char *key, qint64 fallback)
{
    bool ok = false;
    const qint64 value = merged(props, key).toLongLong(&ok);
    return ok ? value : fallback;
}

/// WirePlumber writes the defaults as small JSON objects, `{"name":"…"}`,
/// rather than as bare strings.
QString nameFromDefaultValue(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.startsWith(u'{')) {
        return trimmed;
    }
    const qsizetype nameAt = trimmed.indexOf(u"\"name\""_s);
    if (nameAt < 0) {
        return {};
    }
    const qsizetype colon = trimmed.indexOf(u':', nameAt);
    const qsizetype openQuote = colon < 0 ? -1 : trimmed.indexOf(u'"', colon + 1);
    const qsizetype closeQuote = openQuote < 0 ? -1 : trimmed.indexOf(u'"', openQuote + 1);
    if (closeQuote < 0) {
        return {};
    }
    return trimmed.mid(openQuote + 1, closeQuote - openQuote - 1);
}

} // namespace

/// The proxy of a watched object and the listeners on it: everything that
/// does not depend on what kind of object it is, so that objects of every
/// kind can wait together to be let go.
///
/// **PipeWire may destroy the proxy before we do.** When the server says an
/// object is gone it takes the proxy with it, and the hooks go too; touching
/// either afterwards is a use-after-free, and sending a destroy for it makes
/// the server close the connection on us. So every proxy carries a listener
/// for its own destruction, and forget() is what it leaves behind.
struct WatchedBase {
    /// The global this watches, as the registry announced it.
    std::uint32_t id = 0;
    pw_proxy *proxy = nullptr;
    /// Where a proxy the server never made goes instead of being destroyed.
    std::vector<pw_proxy *> *graveyard = nullptr;
    /// The listener for the object's own events — node info, port info, and
    /// so on.
    spa_hook listener{};
    /// The listener for the proxy's life, which is PipeWire's own.
    spa_hook proxyListener{};
    bool listening = false;
    bool proxyListening = false;

    WatchedBase() = default;
    virtual ~WatchedBase()
    {
        if (listening) {
            spa_hook_remove(&listener);
        }
        if (proxyListening) {
            spa_hook_remove(&proxyListener);
        }
        if (proxy == nullptr) {
            return;
        }
        if (pw_proxy_get_bound_id(proxy) == SPA_ID_INVALID) {
            // **The server never made this one.** A global can go away
            // between our asking to bind it and the server reading the
            // request — a client that connects, looks at the graph and
            // leaves does exactly that — and then there is nothing on the
            // server to destroy. Saying so anyway is a protocol error, and
            // the server rightly closes the connection on a client that
            // commits one, which would take every session node with it.
            //
            // So it waits in the graveyard instead, and is destroyed when
            // the connection is being taken down anyway and a last word to
            // the server can cost nothing.
            if (graveyard != nullptr) {
                graveyard->push_back(proxy);
            }
            return;
        }
        pw_proxy_destroy(proxy);
    }

    WatchedBase(const WatchedBase &) = delete;
    WatchedBase &operator=(const WatchedBase &) = delete;

    /// PipeWire has destroyed the proxy, and the hooks with it. Nothing here
    /// may be touched again.
    void forget() noexcept
    {
        proxy = nullptr;
        listening = false;
        proxyListening = false;
    }
};

/// One object the mirror watches: its proxy, the listener on it, and the
/// typed copy. Held behind a pointer, because a listener is a node of an
/// intrusive list and must never move.
template <typename Value>
struct Watched : WatchedBase {
    RegistryMirror::Private *owner = nullptr;
    Value value;
};

const pw_proxy_events &proxyEvents()
{
    static const pw_proxy_events events = [] {
        pw_proxy_events result{};
        result.version = PW_VERSION_PROXY_EVENTS;
        // PipeWire destroys a proxy itself when the core tells it the object
        // is gone. Everything here belongs to the proxy, so this is the
        // moment to stop pointing at any of it.
        result.destroy = [](void *data) { static_cast<WatchedBase *>(data)->forget(); };
        return result;
    }();
    return events;
}

/// Starts watching the proxy's own life, and says where it goes if it turns
/// out the server never made it. Called for every bind.
void watchProxy(WatchedBase &watched, std::vector<pw_proxy *> &graveyard)
{
    watched.graveyard = &graveyard;
    if (watched.proxy == nullptr) {
        return;
    }
    pw_proxy_add_listener(watched.proxy, &watched.proxyListener, &proxyEvents(), &watched);
    watched.proxyListening = true;
}

class RegistryMirror::Private
{
public:
    Private(RegistryMirror *owner, Connection &theConnection)
        : q(owner)
        , connection(theConnection)
    {
    }

    RegistryMirror *q = nullptr;
    Connection &connection;

    spa_hook registryListener{};
    bool listening = false;

    // std::map keeps them in id order, so every listing is stable without
    // sorting, and holds them by pointer, so a listener never moves.
    std::map<std::uint32_t, std::unique_ptr<Watched<PwNode>>> nodes;
    std::map<std::uint32_t, std::unique_ptr<Watched<PwPort>>> ports;
    std::map<std::uint32_t, std::unique_ptr<Watched<PwLink>>> links;
    std::map<std::uint32_t, std::unique_ptr<Watched<PwClient>>> clients;
    std::map<std::uint32_t, std::unique_ptr<Watched<PwDevice>>> devices;
    /// The "default" metadata. Its typed value is the defaults themselves.
    std::unique_ptr<Watched<PwDefaults>> defaultMetadata;
    /// The "settings" metadata: the graph's clock.
    std::unique_ptr<Watched<PwClockSettings>> settingsMetadata;

    /// Objects the server has taken away, waiting to be let go.
    ///
    /// A proxy is **never** destroyed from inside a registry callback:
    /// PipeWire is in the middle of delivering a batch of messages there and
    /// would come back to memory we had freed. They are moved here instead
    /// and released on the next turn of the Qt event loop, which is after
    /// pw_loop_iterate() has returned.
    std::vector<std::unique_ptr<WatchedBase>> retired;
    /// Proxies the server never made. They cannot be destroyed while the
    /// connection matters; detach() sees to them when it stops mattering.
    std::vector<pw_proxy *> orphans;
    bool flushScheduled = false;

    /// Whether the first picture of the graph is complete.
    ///
    /// A registry announcement says an object exists; what it *is* comes from
    /// binding it and waiting for the server to answer. So the mirror asks
    /// for a round trip, and asks again if anything new turned up while it
    /// waited. When a round trip comes back with nothing new bound since it
    /// was asked for, everything announced has also been described.
    bool ready = false;
    int pendingSync = -1;
    bool boundSinceSync = false;

    void askForSync();
    void onSynced(int seq);

    void retire(std::unique_ptr<WatchedBase> watched);

    PwDefaults defaults;
    /// Where the "default" metadata asks each object to go, by subject: the
    /// `target.object` (or the older `target.node`) a volume applet sets to
    /// move a stream. The policy moves the stream when this changes.
    std::map<std::uint32_t, QString> moveTargets;
    PwClockSettings clock;
    std::uint32_t ownClientId = 0;
    QString ownApplicationName = u"Reverblabr"_s;
    QString ownNodePrefix = u"reverblabr."_s;

    void attach();
    void detach();

    void onGlobal(std::uint32_t id, const char *type, std::uint32_t version, const spa_dict *props);
    void onGlobalRemoved(std::uint32_t id);

    void fillNode(PwNode &node) const;
    /// Takes what a node did not say about the application from the client
    /// that owns it. A stream often carries only its name, while the process
    /// it belongs to is known to its client.
    void fillFromClient(PwNode &node) const;
    /// Fills in every node that belongs to \a clientId, for when the client
    /// is announced after its nodes.
    void refillNodesOfClient(std::uint32_t clientId);
    void fillClient(PwClient &client, const QHash<QString, QString> &props) const;

    [[nodiscard]] bool looksLikeOurs(const PwNode &node) const;
    /// Re-marks every node, for when our own client is announced after some
    /// of our nodes have been.
    void remarkOwnNodes();

    void setDefault(const QString &key, const QString &value);
    void setMoveTarget(std::uint32_t subject, const QString &value);
    void setClockSetting(const QString &key, const QString &value);
};

namespace
{

template <typename Value>
Watched<Value> *watchedOf(void *data)
{
    return static_cast<Watched<Value> *>(data);
}

[[nodiscard]] NodeState nodeStateFrom(pw_node_state state)
{
    switch (state) {
    case PW_NODE_STATE_ERROR:
        return NodeState::Error;
    case PW_NODE_STATE_CREATING:
        return NodeState::Creating;
    case PW_NODE_STATE_SUSPENDED:
        return NodeState::Suspended;
    case PW_NODE_STATE_IDLE:
        return NodeState::Idle;
    case PW_NODE_STATE_RUNNING:
        return NodeState::Running;
    }
    return NodeState::Unknown;
}

const pw_node_events &nodeEvents()
{
    static const pw_node_events events = [] {
        pw_node_events result{};
        result.version = PW_VERSION_NODE_EVENTS;
        result.info = [](void *data, const pw_node_info *info) {
            Watched<PwNode> *watched = watchedOf<PwNode>(data);
            if (info == nullptr) {
                return;
            }
            // What the server is doing with the node: suspended, idle,
            // running. A stream left suspended where it plays is silent.
            if ((info->change_mask & PW_NODE_CHANGE_MASK_STATE) != 0) {
                const NodeState state = nodeStateFrom(info->state);
                if (state != watched->value.state) {
                    watched->value.state = state;
                    Q_EMIT watched->owner->q->nodeStateChanged(watched->value);
                }
            }
            if ((info->change_mask & PW_NODE_CHANGE_MASK_PROPS) == 0) {
                return;
            }
            const QHash<QString, QString> fromInfo = allProps(info->props);
            for (auto entry = fromInfo.cbegin(); entry != fromInfo.cend(); ++entry) {
                watched->value.props.insert(entry.key(), entry.value());
            }
            watched->owner->fillNode(watched->value);
            Q_EMIT watched->owner->q->nodeChanged(watched->value);
        };
        return result;
    }();
    return events;
}

const pw_port_events &portEvents()
{
    static const pw_port_events events = [] {
        pw_port_events result{};
        result.version = PW_VERSION_PORT_EVENTS;
        result.info = [](void *data, const pw_port_info *info) {
            Watched<PwPort> *watched = watchedOf<PwPort>(data);
            if (info == nullptr) {
                return;
            }
            watched->value.direction =
                info->direction == SPA_DIRECTION_INPUT ? PortDirection::Input : PortDirection::Output;
            if ((info->change_mask & PW_PORT_CHANGE_MASK_PROPS) != 0) {
                const QHash<QString, QString> fromInfo = allProps(info->props);
                for (auto entry = fromInfo.cbegin(); entry != fromInfo.cend(); ++entry) {
                    watched->value.props.insert(entry.key(), entry.value());
                }
                readPortProperties(watched->value);
            }
        };
        return result;
    }();
    return events;
}

[[nodiscard]] LinkState linkStateFrom(pw_link_state state)
{
    switch (state) {
    case PW_LINK_STATE_ERROR:
        return LinkState::Error;
    case PW_LINK_STATE_UNLINKED:
        return LinkState::Unlinked;
    case PW_LINK_STATE_INIT:
        return LinkState::Init;
    case PW_LINK_STATE_NEGOTIATING:
        return LinkState::Negotiating;
    case PW_LINK_STATE_ALLOCATING:
        return LinkState::Allocating;
    case PW_LINK_STATE_PAUSED:
        return LinkState::Paused;
    case PW_LINK_STATE_ACTIVE:
        return LinkState::Active;
    }
    return LinkState::Unknown;
}

const pw_link_events &linkEvents()
{
    static const pw_link_events events = [] {
        pw_link_events result{};
        result.version = PW_VERSION_LINK_EVENTS;
        result.info = [](void *data, const pw_link_info *info) {
            Watched<PwLink> *watched = watchedOf<PwLink>(data);
            if (info == nullptr) {
                return;
            }
            watched->value.outputNode = info->output_node_id;
            watched->value.outputPort = info->output_port_id;
            watched->value.inputNode = info->input_node_id;
            watched->value.inputPort = info->input_port_id;
            if ((info->change_mask & PW_LINK_CHANGE_MASK_STATE) != 0) {
                const LinkState state = linkStateFrom(info->state);
                if (state != watched->value.state) {
                    watched->value.state = state;
                    Q_EMIT watched->owner->q->linkStateChanged(watched->value);
                }
            }
        };
        return result;
    }();
    return events;
}

const pw_client_events &clientEvents()
{
    static const pw_client_events events = [] {
        pw_client_events result{};
        result.version = PW_VERSION_CLIENT_EVENTS;
        result.info = [](void *data, const pw_client_info *info) {
            Watched<PwClient> *watched = watchedOf<PwClient>(data);
            if (info == nullptr || (info->change_mask & PW_CLIENT_CHANGE_MASK_PROPS) == 0) {
                return;
            }
            const QHash<QString, QString> fromInfo = allProps(info->props);
            for (auto entry = fromInfo.cbegin(); entry != fromInfo.cend(); ++entry) {
                watched->value.props.insert(entry.key(), entry.value());
            }
            watched->owner->fillClient(watched->value, watched->value.props);
            if (watched->value.isUs) {
                watched->owner->ownClientId = watched->value.object.id;
                watched->owner->remarkOwnNodes();
            }
            watched->owner->refillNodesOfClient(watched->value.object.id);
        };
        return result;
    }();
    return events;
}

const pw_device_events &deviceEvents()
{
    static const pw_device_events events = [] {
        pw_device_events result{};
        result.version = PW_VERSION_DEVICE_EVENTS;
        result.info = [](void *data, const pw_device_info *info) {
            Watched<PwDevice> *watched = watchedOf<PwDevice>(data);
            if (info == nullptr || (info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS) == 0) {
                return;
            }
            const QHash<QString, QString> fromInfo = allProps(info->props);
            for (auto entry = fromInfo.cbegin(); entry != fromInfo.cend(); ++entry) {
                watched->value.props.insert(entry.key(), entry.value());
            }
            readDeviceProperties(watched->value);
        };
        return result;
    }();
    return events;
}

const pw_metadata_events &settingsEvents()
{
    static const pw_metadata_events events = [] {
        pw_metadata_events result{};
        result.version = PW_VERSION_METADATA_EVENTS;
        result.property =
            [](void *data, std::uint32_t subject, const char *key, const char *type, const char *value) -> int {
            Q_UNUSED(type)
            Watched<PwClockSettings> *watched = watchedOf<PwClockSettings>(data);
            if (subject != 0 || key == nullptr) {
                return 0;
            }
            watched->owner->setClockSetting(QString::fromUtf8(key),
                                            value == nullptr ? QString{} : QString::fromUtf8(value));
            return 0;
        };
        return result;
    }();
    return events;
}

const pw_metadata_events &metadataEvents()
{
    static const pw_metadata_events events = [] {
        pw_metadata_events result{};
        result.version = PW_VERSION_METADATA_EVENTS;
        result.property =
            [](void *data, std::uint32_t subject, const char *key, const char *type, const char *value) -> int {
            Q_UNUSED(type)
            Watched<PwDefaults> *watched = watchedOf<PwDefaults>(data);
            if (key == nullptr) {
                return 0;
            }
            const QString text = value == nullptr ? QString{} : QString::fromUtf8(value);
            // Subject 0 is the session's own settings. Any other subject is one
            // object, and the only thing read about it is where it has been
            // asked to go: that is a stream being moved.
            if (subject != 0) {
                if (std::strcmp(key, "target.object") == 0 || std::strcmp(key, "target.node") == 0) {
                    watched->owner->setMoveTarget(subject, text);
                }
                return 0;
            }
            watched->owner->setDefault(QString::fromUtf8(key), text);
            return 0;
        };
        return result;
    }();
    return events;
}

const pw_registry_events &registryEvents()
{
    static const pw_registry_events events = [] {
        pw_registry_events result{};
        result.version = PW_VERSION_REGISTRY_EVENTS;
        result.global = [](void *data,
                           std::uint32_t id,
                           std::uint32_t permissions,
                           const char *type,
                           std::uint32_t version,
                           const spa_dict *props) {
            Q_UNUSED(permissions)
            static_cast<RegistryMirror::Private *>(data)->onGlobal(id, type, version, props);
        };
        result.global_remove = [](void *data, std::uint32_t id) {
            static_cast<RegistryMirror::Private *>(data)->onGlobalRemoved(id);
        };
        return result;
    }();
    return events;
}

} // namespace

void RegistryMirror::Private::fillNode(PwNode &node) const
{
    readNodeProperties(node);
    fillFromClient(node);
    node.isOurs = looksLikeOurs(node);
}

void RegistryMirror::Private::fillClient(PwClient &client, const QHash<QString, QString> &props) const
{
    client.applicationName = merged(props, PW_KEY_APP_NAME);
    client.applicationBinary = merged(props, PW_KEY_APP_PROCESS_BINARY);
    client.applicationPid = mergedNumber(props, PW_KEY_APP_PROCESS_ID, -1);
    // Our own connection is the one the server gave this id to. A process can
    // hold more than one — a test harness does, and so does any library in
    // the process that talks to PipeWire itself — so "the client with our
    // process id" names more than one, and taking the last one seen would
    // make which nodes count as ours depend on the order they arrived in.
    // Before the server has told us our id, the process is the best we have.
    const std::uint32_t ours = connection.clientId();
    client.isUs = ours != 0 ? client.object.id == ours : client.applicationPid == static_cast<qint64>(::getpid());
}

void RegistryMirror::Private::fillFromClient(PwNode &node) const
{
    if (node.clientId == 0) {
        return;
    }
    const auto found = clients.find(node.clientId);
    if (found == clients.end()) {
        return; // the client may be announced after its nodes
    }
    const PwClient &client = found->second->value;
    if (node.applicationName.isEmpty()) {
        node.applicationName = client.applicationName;
    }
    if (node.applicationBinary.isEmpty()) {
        node.applicationBinary = client.applicationBinary;
    }
    if (node.applicationPid < 0) {
        node.applicationPid = client.applicationPid;
    }
}

void RegistryMirror::Private::refillNodesOfClient(std::uint32_t clientId)
{
    for (const auto &[id, watched] : nodes) {
        if (watched->value.clientId != clientId) {
            continue;
        }
        const PwNode before = watched->value;
        fillFromClient(watched->value);
        watched->value.isOurs = looksLikeOurs(watched->value);
        if (watched->value.applicationPid != before.applicationPid
            || watched->value.applicationName != before.applicationName
            || watched->value.applicationBinary != before.applicationBinary || watched->value.isOurs != before.isOurs) {
            Q_EMIT q->nodeChanged(watched->value);
        }
    }
}

bool RegistryMirror::Private::looksLikeOurs(const PwNode &node) const
{
    // **A virtual device we made is a device, never ours** (ADR-0013). It is
    // meant to be seen and picked, in our own lists as in everybody else's,
    // and it names no client, so nothing below would say otherwise today;
    // this says it before any rule about names or clients can.
    if (node.props.contains(
            QString::fromLatin1(kVirtualDeviceMarker.data(), static_cast<qsizetype>(kVirtualDeviceMarker.size())))) {
        return false;
    }
    if (ownClientId != 0 && node.clientId == ownClientId) {
        return true;
    }
    if (!ownApplicationName.isEmpty() && node.applicationName == ownApplicationName) {
        return true;
    }
    return !ownNodePrefix.isEmpty() && node.name.startsWith(ownNodePrefix);
}

void RegistryMirror::Private::remarkOwnNodes()
{
    for (const auto &[id, watched] : nodes) {
        const bool wasOurs = watched->value.isOurs;
        watched->value.isOurs = looksLikeOurs(watched->value);
        if (watched->value.isOurs != wasOurs) {
            Q_EMIT q->nodeChanged(watched->value);
        }
    }
}

void RegistryMirror::Private::setDefault(const QString &key, const QString &value)
{
    const QString name = nameFromDefaultValue(value);
    const PwDefaults before = defaults;
    if (key == u"default.audio.sink"_s) {
        defaults.audioSink = name;
    } else if (key == u"default.audio.source"_s) {
        defaults.audioSource = name;
    } else if (key == u"default.configured.audio.sink"_s) {
        defaults.configuredAudioSink = name;
    } else if (key == u"default.configured.audio.source"_s) {
        defaults.configuredAudioSource = name;
    } else {
        return;
    }
    if (defaults != before) {
        Q_EMIT q->defaultsChanged(defaults);
    }
}

void RegistryMirror::Private::setMoveTarget(std::uint32_t subject, const QString &value)
{
    const auto known = moveTargets.find(subject);
    const QString before = known == moveTargets.end() ? QString{} : known->second;
    if (value.isEmpty()) {
        moveTargets.erase(subject);
    } else {
        moveTargets[subject] = value;
    }
    // **Only a change made while we watch is a move.** The metadata tells a
    // new listener everything it already holds, and that is where streams
    // are, not where they are being sent.
    if (ready && value != before) {
        Q_EMIT q->moveRequested(subject);
    }
}

void RegistryMirror::Private::setClockSetting(const QString &key, const QString &value)
{
    // A key taken away (a forced quantum let go) is nought, which is what
    // "not forced" is.
    bool ok = false;
    const std::uint32_t number = value.trimmed().toUInt(&ok);
    const std::uint32_t read = ok ? number : 0;
    const PwClockSettings before = clock;
    if (key == u"clock.rate"_s) {
        clock.rate = read;
    } else if (key == u"clock.quantum"_s) {
        clock.quantum = read;
    } else if (key == u"clock.min-quantum"_s) {
        clock.minQuantum = read;
    } else if (key == u"clock.max-quantum"_s) {
        clock.maxQuantum = read;
    } else if (key == u"clock.force-rate"_s) {
        clock.forceRate = read;
    } else if (key == u"clock.force-quantum"_s) {
        clock.forceQuantum = read;
    } else {
        return;
    }
    if (clock != before) {
        Q_EMIT q->clockSettingsChanged(clock);
    }
}

void RegistryMirror::Private::onGlobal(std::uint32_t id, const char *type, std::uint32_t version, const spa_dict *props)
{
    if (type == nullptr) {
        return;
    }
    pw_registry *registry = connection.registry();
    if (registry == nullptr) {
        return;
    }
    const QHash<QString, QString> announced = allProps(props);
    const PwObjectId object{id, static_cast<std::uint64_t>(mergedNumber(announced, PW_KEY_OBJECT_SERIAL, 0))};

    const auto bind = [&](const char *interface, std::uint32_t interfaceVersion, std::size_t size) {
        boundSinceSync = true;
        return pw_registry_bind(registry, id, interface, std::min(version, interfaceVersion), size);
    };

    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        auto watched = std::make_unique<Watched<PwNode>>();
        watched->owner = this;
        watched->id = id;
        watched->value.object = object;
        watched->value.props = announced;
        fillNode(watched->value);
        watched->proxy = static_cast<pw_proxy *>(bind(PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
        if (watched->proxy != nullptr) {
            pw_node_add_listener(
                reinterpret_cast<pw_node *>(watched->proxy), &watched->listener, &nodeEvents(), watched.get());
            watched->listening = true;
        }
        watchProxy(*watched, orphans);
        const PwNode value = watched->value;
        nodes.emplace(id, std::move(watched));
        Q_EMIT q->nodeAdded(value);
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
        auto watched = std::make_unique<Watched<PwPort>>();
        watched->owner = this;
        watched->id = id;
        watched->value.object = object;
        watched->value.props = announced;
        readPortProperties(watched->value);
        watched->proxy = static_cast<pw_proxy *>(bind(PW_TYPE_INTERFACE_Port, PW_VERSION_PORT, 0));
        if (watched->proxy != nullptr) {
            pw_port_add_listener(
                reinterpret_cast<pw_port *>(watched->proxy), &watched->listener, &portEvents(), watched.get());
            watched->listening = true;
        }
        watchProxy(*watched, orphans);
        const PwPort value = watched->value;
        ports.emplace(id, std::move(watched));
        Q_EMIT q->portAdded(value);
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        auto watched = std::make_unique<Watched<PwLink>>();
        watched->owner = this;
        watched->id = id;
        watched->value.object = object;
        watched->value.outputNode = static_cast<std::uint32_t>(mergedNumber(announced, PW_KEY_LINK_OUTPUT_NODE, 0));
        watched->value.outputPort = static_cast<std::uint32_t>(mergedNumber(announced, PW_KEY_LINK_OUTPUT_PORT, 0));
        watched->value.inputNode = static_cast<std::uint32_t>(mergedNumber(announced, PW_KEY_LINK_INPUT_NODE, 0));
        watched->value.inputPort = static_cast<std::uint32_t>(mergedNumber(announced, PW_KEY_LINK_INPUT_PORT, 0));
        watched->proxy = static_cast<pw_proxy *>(bind(PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, 0));
        if (watched->proxy != nullptr) {
            pw_link_add_listener(
                reinterpret_cast<pw_link *>(watched->proxy), &watched->listener, &linkEvents(), watched.get());
            watched->listening = true;
        }
        watchProxy(*watched, orphans);
        const PwLink value = watched->value;
        links.emplace(id, std::move(watched));
        Q_EMIT q->linkAdded(value);
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Client) == 0) {
        auto watched = std::make_unique<Watched<PwClient>>();
        watched->owner = this;
        watched->id = id;
        watched->value.object = object;
        watched->value.props = announced;
        fillClient(watched->value, announced);
        watched->proxy = static_cast<pw_proxy *>(bind(PW_TYPE_INTERFACE_Client, PW_VERSION_CLIENT, 0));
        if (watched->proxy != nullptr) {
            pw_client_add_listener(
                reinterpret_cast<pw_client *>(watched->proxy), &watched->listener, &clientEvents(), watched.get());
            watched->listening = true;
        }
        watchProxy(*watched, orphans);
        const PwClient value = watched->value;
        if (value.isUs) {
            ownClientId = id;
        }
        clients.emplace(id, std::move(watched));
        if (value.isUs) {
            remarkOwnNodes();
        }
        refillNodesOfClient(id);
        Q_EMIT q->clientAdded(value);
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
        auto watched = std::make_unique<Watched<PwDevice>>();
        watched->owner = this;
        watched->id = id;
        watched->value.object = object;
        watched->value.props = announced;
        readDeviceProperties(watched->value);
        watched->proxy = static_cast<pw_proxy *>(bind(PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0));
        if (watched->proxy != nullptr) {
            pw_device_add_listener(
                reinterpret_cast<pw_device *>(watched->proxy), &watched->listener, &deviceEvents(), watched.get());
            watched->listening = true;
        }
        watchProxy(*watched, orphans);
        const PwDevice value = watched->value;
        devices.emplace(id, std::move(watched));
        Q_EMIT q->deviceAdded(value);
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0
        && merged(announced, PW_KEY_METADATA_NAME) == QString::fromUtf8(kSettingsMetadataName)) {
        auto watched = std::make_unique<Watched<PwClockSettings>>();
        watched->owner = this;
        watched->id = id;
        watched->proxy = static_cast<pw_proxy *>(bind(PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
        if (watched->proxy != nullptr) {
            pw_metadata_add_listener(
                reinterpret_cast<pw_metadata *>(watched->proxy), &watched->listener, &settingsEvents(), watched.get());
            watched->listening = true;
        }
        watchProxy(*watched, orphans);
        settingsMetadata = std::move(watched);
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        if (merged(announced, PW_KEY_METADATA_NAME) != QString::fromUtf8(kDefaultMetadataName)) {
            return; // there are several; only the defaults and the settings matter
        }
        auto watched = std::make_unique<Watched<PwDefaults>>();
        watched->owner = this;
        watched->id = id;
        watched->proxy = static_cast<pw_proxy *>(bind(PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
        if (watched->proxy != nullptr) {
            pw_metadata_add_listener(
                reinterpret_cast<pw_metadata *>(watched->proxy), &watched->listener, &metadataEvents(), watched.get());
            watched->listening = true;
        }
        watchProxy(*watched, orphans);
        defaultMetadata = std::move(watched);
        return;
    }
}

void RegistryMirror::Private::retire(std::unique_ptr<WatchedBase> watched)
{
    retired.push_back(std::move(watched));
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

void RegistryMirror::Private::onGlobalRemoved(std::uint32_t id)
{
    if (const auto found = nodes.find(id); found != nodes.end()) {
        const PwObjectId object = found->second->value.object;
        retire(std::move(found->second));
        nodes.erase(found);
        moveTargets.erase(object.id);
        Q_EMIT q->nodeRemoved(object.id, object.serial);
        return;
    }
    if (const auto found = ports.find(id); found != ports.end()) {
        const PwObjectId object = found->second->value.object;
        retire(std::move(found->second));
        ports.erase(found);
        Q_EMIT q->portRemoved(object.id, object.serial);
        return;
    }
    if (const auto found = links.find(id); found != links.end()) {
        const PwObjectId object = found->second->value.object;
        retire(std::move(found->second));
        links.erase(found);
        Q_EMIT q->linkRemoved(object.id, object.serial);
        return;
    }
    if (const auto found = clients.find(id); found != clients.end()) {
        const PwObjectId object = found->second->value.object;
        const bool wasUs = found->second->value.isUs;
        retire(std::move(found->second));
        clients.erase(found);
        if (wasUs) {
            ownClientId = 0;
        }
        Q_EMIT q->clientRemoved(object.id, object.serial);
        return;
    }
    if (const auto found = devices.find(id); found != devices.end()) {
        const PwObjectId object = found->second->value.object;
        retire(std::move(found->second));
        devices.erase(found);
        Q_EMIT q->deviceRemoved(object.id, object.serial);
        return;
    }
    if (defaultMetadata != nullptr && defaultMetadata->id == id) {
        retire(std::move(defaultMetadata));
        defaultMetadata.reset();
        defaults = {};
        Q_EMIT q->defaultsChanged(defaults);
    }
    if (settingsMetadata != nullptr && settingsMetadata->id == id) {
        retire(std::move(settingsMetadata));
        settingsMetadata.reset();
        clock = {};
        Q_EMIT q->clockSettingsChanged(clock);
    }
}

void RegistryMirror::Private::askForSync()
{
    boundSinceSync = false;
    pendingSync = connection.sync();
}

void RegistryMirror::Private::onSynced(int seq)
{
    if (ready || seq != pendingSync) {
        return;
    }
    if (boundSinceSync) {
        askForSync(); // something new turned up; wait for its answers too
        return;
    }
    ready = true;
    Q_EMIT q->ready();
}

void RegistryMirror::Private::attach()
{
    pw_registry *registry = connection.registry();
    if (registry == nullptr || listening) {
        return;
    }
    pw_registry_add_listener(registry, &registryListener, &registryEvents(), this);
    listening = true;
    askForSync();
}

void RegistryMirror::Private::detach()
{
    // The proxies go first, while the core they belong to is still there.
    // Anything already waiting to be let go goes now, for the same reason:
    // the queued flush would otherwise run after the core had gone.
    retired.clear();
    nodes.clear();
    ports.clear();
    links.clear();
    clients.clear();
    devices.clear();
    defaultMetadata.reset();
    settingsMetadata.reset();
    moveTargets.clear();

    // And the proxies the server never made, which could not be destroyed
    // while the connection mattered: it is going away now, so a last word to
    // the server about an object it never had costs nothing.
    for (pw_proxy *orphan : orphans) {
        pw_proxy_destroy(orphan);
    }
    orphans.clear();

    ownClientId = 0;
    defaults = {};
    clock = {};

    if (listening) {
        spa_hook_remove(&registryListener);
        listening = false;
    }
    ready = false;
    pendingSync = -1;
    boundSinceSync = false;
    Q_EMIT q->cleared();
}

RegistryMirror::RegistryMirror(Connection &connection, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, connection))
{
    connect(&connection, &Connection::registryReady, this, [this] { d->attach(); });
    connect(&connection, &Connection::registryGone, this, [this] { d->detach(); });
    connect(&connection, &Connection::synced, this, [this](int seq) { d->onSynced(seq); });
    if (connection.isConnected()) {
        d->attach();
    }
}

RegistryMirror::~RegistryMirror()
{
    d->detach();
}

QList<PwNode> RegistryMirror::nodes() const
{
    QList<PwNode> result;
    result.reserve(static_cast<qsizetype>(d->nodes.size()));
    for (const auto &[id, watched] : d->nodes) {
        result.append(watched->value);
    }
    return result;
}

QList<PwPort> RegistryMirror::ports() const
{
    QList<PwPort> result;
    result.reserve(static_cast<qsizetype>(d->ports.size()));
    for (const auto &[id, watched] : d->ports) {
        result.append(watched->value);
    }
    return result;
}

QList<PwLink> RegistryMirror::links() const
{
    QList<PwLink> result;
    result.reserve(static_cast<qsizetype>(d->links.size()));
    for (const auto &[id, watched] : d->links) {
        result.append(watched->value);
    }
    return result;
}

QList<PwClient> RegistryMirror::clients() const
{
    QList<PwClient> result;
    result.reserve(static_cast<qsizetype>(d->clients.size()));
    for (const auto &[id, watched] : d->clients) {
        result.append(watched->value);
    }
    return result;
}

QList<PwDevice> RegistryMirror::devices() const
{
    QList<PwDevice> result;
    result.reserve(static_cast<qsizetype>(d->devices.size()));
    for (const auto &[id, watched] : d->devices) {
        result.append(watched->value);
    }
    return result;
}

PwDefaults RegistryMirror::defaults() const
{
    return d->defaults;
}

PwClockSettings RegistryMirror::clockSettings() const
{
    return d->clock;
}

std::optional<PwNode> RegistryMirror::graphDriver(std::optional<std::uint32_t> following) const
{
    // The driver a node names, when it names one that is there.
    const auto driverOf = [this](const PwNode &node) -> std::optional<PwNode> {
        if (node.driverId != 0) {
            if (std::optional<PwNode> driver = this->node(node.driverId)) {
                return driver;
            }
        }
        // A device that drives its own graph may not name itself.
        return node.canDrive ? std::optional<PwNode>{node} : std::nullopt;
    };
    if (following) {
        if (const std::optional<PwNode> ours = node(*following)) {
            if (std::optional<PwNode> driver = driverOf(*ours)) {
                return driver;
            }
        }
    }
    if (const std::optional<PwNode> sink = defaultSink()) {
        return driverOf(*sink);
    }
    return std::nullopt;
}

std::optional<PwNode> RegistryMirror::node(std::uint32_t id) const
{
    const auto found = d->nodes.find(id);
    return found == d->nodes.end() ? std::nullopt : std::optional{found->second->value};
}

std::optional<PwNode> RegistryMirror::nodeBySerial(std::uint64_t serial) const
{
    for (const auto &[id, watched] : d->nodes) {
        if (watched->value.object.serial == serial) {
            return watched->value;
        }
    }
    return std::nullopt;
}

std::optional<PwNode> RegistryMirror::nodeNamed(QStringView name) const
{
    for (const auto &[id, watched] : d->nodes) {
        if (watched->value.name == name) {
            return watched->value;
        }
    }
    return std::nullopt;
}

std::optional<PwPort> RegistryMirror::port(std::uint32_t id) const
{
    const auto found = d->ports.find(id);
    return found == d->ports.end() ? std::nullopt : std::optional{found->second->value};
}

std::optional<PwLink> RegistryMirror::link(std::uint32_t id) const
{
    const auto found = d->links.find(id);
    return found == d->links.end() ? std::nullopt : std::optional{found->second->value};
}

std::optional<PwClient> RegistryMirror::client(std::uint32_t id) const
{
    const auto found = d->clients.find(id);
    return found == d->clients.end() ? std::nullopt : std::optional{found->second->value};
}

std::optional<PwDevice> RegistryMirror::device(std::uint32_t id) const
{
    const auto found = d->devices.find(id);
    return found == d->devices.end() ? std::nullopt : std::optional{found->second->value};
}

QList<PwPort> RegistryMirror::portsOf(std::uint32_t nodeId, PortDirection direction) const
{
    QList<PwPort> result;
    for (const auto &[id, watched] : d->ports) {
        if (watched->value.nodeId == nodeId && watched->value.direction == direction) {
            result.append(watched->value);
        }
    }
    std::ranges::sort(result, {}, [](const PwPort &port) { return port.index; });
    return result;
}

QList<PwNode> RegistryMirror::nodesOfKind(NodeKind kind) const
{
    QList<PwNode> result;
    for (const auto &[id, watched] : d->nodes) {
        if (watched->value.kind == kind) {
            result.append(watched->value);
        }
    }
    return result;
}

bool RegistryMirror::isLinked(std::uint32_t outputNode, std::uint32_t inputNode) const
{
    for (const auto &[id, watched] : d->links) {
        if (watched->value.outputNode == outputNode && watched->value.inputNode == inputNode) {
            return true;
        }
    }
    return false;
}

QList<PwLink> RegistryMirror::linksOf(std::uint32_t nodeId) const
{
    QList<PwLink> result;
    for (const auto &[id, watched] : d->links) {
        if (watched->value.outputNode == nodeId || watched->value.inputNode == nodeId) {
            result.append(watched->value);
        }
    }
    return result;
}

QString RegistryMirror::moveTarget(std::uint32_t nodeId) const
{
    const auto found = d->moveTargets.find(nodeId);
    return found == d->moveTargets.end() ? QString{} : found->second;
}

std::optional<PwNode> RegistryMirror::defaultSink() const
{
    return d->defaults.audioSink.isEmpty() ? std::nullopt : nodeNamed(d->defaults.audioSink);
}

std::optional<PwNode> RegistryMirror::defaultSource() const
{
    return d->defaults.audioSource.isEmpty() ? std::nullopt : nodeNamed(d->defaults.audioSource);
}

void RegistryMirror::setOwnApplicationName(QString name)
{
    d->ownApplicationName = std::move(name);
    d->remarkOwnNodes();
}

void RegistryMirror::setOwnNodeNamePrefix(QString prefix)
{
    d->ownNodePrefix = std::move(prefix);
    d->remarkOwnNodes();
}

bool RegistryMirror::isReady() const
{
    return d->ready;
}

std::size_t RegistryMirror::size() const
{
    return d->nodes.size() + d->ports.size() + d->links.size() + d->clients.size() + d->devices.size();
}

} // namespace rb::pw
