// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/virtualdevicemanager.h"

#include "pw/connection.h"
#include "pw/logging.h"
#include "pw/pwobjects.h"
#include "pw/registrymirror.h"

#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <pipewire/pipewire.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::pw
{

std::string_view virtualDeviceStatusName(VirtualDeviceStatus status) noexcept
{
    switch (status) {
    case VirtualDeviceStatus::Disabled:
        return "disabled";
    case VirtualDeviceStatus::Present:
        return "present";
    case VirtualDeviceStatus::Creating:
        return "creating";
    case VirtualDeviceStatus::Missing:
        return "missing";
    case VirtualDeviceStatus::WaitingForPipeWire:
        return "waitingForPipeWire";
    }
    return "unknown";
}

QJsonArray virtualDevicesToJson(const QList<VirtualDeviceState> &devices)
{
    QJsonArray array;
    for (const VirtualDeviceState &state : devices) {
        const VirtualDevice &device = state.device;
        const std::string_view status = virtualDeviceStatusName(state.status);
        array.append(QJsonObject{
            {u"id"_s, device.id},
            {u"name"_s, device.name},
            {u"nodeName"_s, virtualDeviceNodeName(device.id)},
            {u"direction"_s, device.direction == VirtualDeviceDirection::Output ? u"output"_s : u"input"_s},
            {u"channels"_s, device.channels == VirtualDeviceChannels::Stereo ? u"stereo"_s : u"mono"_s},
            {u"enabled"_s, device.enabled},
            {u"scope"_s, virtualDeviceScopeName(device.scope)},
            {u"status"_s, QString::fromLatin1(status.data(), static_cast<qsizetype>(status.size()))},
        });
    }
    return array;
}

namespace
{

constexpr std::array kScopes{VirtualDeviceScope::User, VirtualDeviceScope::System};

[[nodiscard]] std::size_t indexOf(VirtualDeviceScope scope)
{
    return scope == VirtualDeviceScope::User ? 0 : 1;
}

[[nodiscard]] QString markerKey()
{
    return QString::fromLatin1(kVirtualDeviceMarker.data(), static_cast<qsizetype>(kVirtualDeviceMarker.size()));
}

/// The scope a marked node says it comes from, or nothing for a node that
/// is not one of these devices or names a scope we do not know.
[[nodiscard]] std::optional<VirtualDeviceScope> markedScope(const PwNode &node)
{
    const auto found = node.props.constFind(markerKey());
    if (found == node.props.cend()) {
        return std::nullopt;
    }
    if (*found == virtualDeviceScopeName(VirtualDeviceScope::User)) {
        return VirtualDeviceScope::User;
    }
    if (*found == virtualDeviceScopeName(VirtualDeviceScope::System)) {
        return VirtualDeviceScope::System;
    }
    return std::nullopt;
}

} // namespace

/// A node asked for and not yet seen in the graph, and the proxy that asked.
///
/// **Letting go of the proxy does not take the node with it**: it was made
/// with `object.linger = true`, which is the whole point. The proxy is kept
/// only to hear PipeWire refuse, and is let go once the node is there.
struct PendingNode {
    VirtualDeviceManager::Private *owner = nullptr;
    QString deviceId;
    QString nodeName;
    /// The nodes of that name that were already there, by serial, so that a
    /// rename's new node is told apart from the one it replaces.
    QSet<std::uint64_t> before;
    pw_proxy *proxy = nullptr;
    spa_hook listener{};
    bool listening = false;

    PendingNode() = default;
    ~PendingNode()
    {
        if (listening) {
            spa_hook_remove(&listener);
        }
        if (proxy != nullptr) {
            pw_proxy_destroy(proxy);
        }
    }

    PendingNode(const PendingNode &) = delete;
    PendingNode &operator=(const PendingNode &) = delete;
};

class VirtualDeviceManager::Private
{
public:
    Private(VirtualDeviceManager *owner, Connection &theConnection, RegistryMirror &theMirror)
        : q(owner)
        , connection(theConnection)
        , mirror(theMirror)
    {
    }

    VirtualDeviceManager *q;
    Connection &connection;
    RegistryMirror &mirror;

    std::array<VirtualDeviceStore, 2> stores{VirtualDeviceStore{VirtualDeviceScope::User},
                                             VirtualDeviceStore{VirtualDeviceScope::System}};
    std::array<QList<VirtualDevice>, 2> lists;
    std::array<std::optional<VirtualDeviceError>, 2> loadErrors;

    /// By node name: one request at a time for each.
    std::map<QString, std::unique_ptr<PendingNode>> pending;
    /// Proxies whose callbacks are still on the stack; let go of on the next
    /// turn of the event loop.
    std::vector<std::unique_ptr<PendingNode>> retired;
    bool flushScheduled = false;
    /// Devices PipeWire would not make, until they are next asked for.
    QSet<QString> refused;
    /// Nodes asked to go and not yet gone, so a pass does not ask twice.
    std::set<std::uint32_t> destroying;

    QTimer *timer = nullptr;
    bool fullPassWanted = false;
    QList<VirtualDeviceState> announced;

    void load()
    {
        for (VirtualDeviceScope scope : kScopes) {
            const std::size_t index = indexOf(scope);
            std::expected<QList<VirtualDevice>, VirtualDeviceError> read = stores[index].load();
            if (read) {
                lists[index] = std::move(*read);
                loadErrors[index].reset();
            } else {
                lists[index].clear();
                loadErrors[index] = read.error();
                qCWarning(RB_PW) << "cannot read the virtual devices of" << virtualDeviceScopeName(scope) << "from"
                                 << stores[index].path() << ":" << virtualDeviceErrorName(read.error()).data();
            }
        }
    }

    [[nodiscard]] VirtualDevice *find(const QString &id)
    {
        for (QList<VirtualDevice> &list : lists) {
            for (VirtualDevice &device : list) {
                if (device.id == id) {
                    return &device;
                }
            }
        }
        return nullptr;
    }

    /// Writes the user's list with \a change made to it, and keeps it only
    /// once it is on the disk.
    [[nodiscard]] std::expected<void, VirtualDeviceError> saveUser(QList<VirtualDevice> changed)
    {
        const std::size_t index = indexOf(VirtualDeviceScope::User);
        if (loadErrors[index]) {
            // A file we could not read may hold devices the user still
            // wants; writing ours over it would lose them.
            return std::unexpected(*loadErrors[index]);
        }
        if (std::expected<void, VirtualDeviceError> saved = stores[index].save(changed); !saved) {
            return saved;
        }
        lists[index] = std::move(changed);
        return {};
    }

    [[nodiscard]] VirtualDeviceStatus statusOf(const VirtualDevice &device) const
    {
        if (!device.enabled) {
            return VirtualDeviceStatus::Disabled;
        }
        if (!mirror.isReady()) {
            return VirtualDeviceStatus::WaitingForPipeWire;
        }
        const QString name = virtualDeviceNodeName(device.id);
        if (mirror.nodeNamed(name)) {
            return VirtualDeviceStatus::Present;
        }
        if (pending.contains(name)) {
            return VirtualDeviceStatus::Creating;
        }
        return VirtualDeviceStatus::Missing;
    }

    [[nodiscard]] QList<VirtualDeviceState> states() const
    {
        QList<VirtualDeviceState> result;
        for (const QList<VirtualDevice> &list : lists) {
            for (const VirtualDevice &device : list) {
                result.append({device, statusOf(device)});
            }
        }
        return result;
    }

    void announce()
    {
        QList<VirtualDeviceState> now = states();
        if (now != announced) {
            announced = std::move(now);
            Q_EMIT q->devicesChanged();
        }
    }

    /// Asks for a pass. A full one creates and removes; the lighter one,
    /// which every graph change asks for, only finishes what is under way:
    /// it lets go of the requests whose nodes have appeared, and destroys the
    /// nodes a rename has replaced. **A device that something else removed
    /// is not made again by a graph change**, only by a full pass.
    void schedule(bool full)
    {
        fullPassWanted = fullPassWanted || full;
        if (!timer->isActive()) {
            timer->start();
        }
    }

    void runScheduled()
    {
        const bool full = fullPassWanted;
        fullPassWanted = false;
        reconcile(full);
    }

    void reconcile(bool full);
    void create(const VirtualDevice &device, const QList<PwNode> &sameName);
    void destroy(const PwNode &node);
    void completePending(const QList<PwNode> &nodes);
    void refuse(PendingNode *request, const QString &detail);
    void retire(std::unique_ptr<PendingNode> request);
    void forgetGraph();
};

namespace
{

const pw_proxy_events &pendingProxyEvents()
{
    static const pw_proxy_events events = [] {
        pw_proxy_events result{};
        result.version = PW_VERSION_PROXY_EVENTS;
        result.error = [](void *data, int seq, int res, const char *message) {
            Q_UNUSED(seq)
            auto *request = static_cast<PendingNode *>(data);
            request->owner->refuse(request,
                                   u"%1 (%2)"_s.arg(QString::fromUtf8(message == nullptr ? "" : message)).arg(res));
        };
        return result;
    }();
    return events;
}

} // namespace

void VirtualDeviceManager::Private::reconcile(bool full)
{
    if (!mirror.isReady() || connection.core() == nullptr || connection.registry() == nullptr) {
        announce();
        return;
    }
    const QList<PwNode> nodes = mirror.nodes();
    completePending(nodes);

    std::array<QSet<QString>, 2> wanted;
    for (VirtualDeviceScope scope : kScopes) {
        const std::size_t index = indexOf(scope);
        if (loadErrors[index]) {
            continue;
        }
        for (const VirtualDevice &device : lists[index]) {
            if (!device.enabled) {
                continue;
            }
            const QString name = virtualDeviceNodeName(device.id);
            wanted[index].insert(name);

            QList<PwNode> sameName;
            for (const PwNode &node : nodes) {
                if (node.name == name && !destroying.contains(node.object.id)) {
                    sameName.append(node);
                }
            }
            const QString mediaClass = virtualDeviceMediaClass(device.direction);
            const PwNode *newest = nullptr;
            for (const PwNode &node : sameName) {
                const bool right =
                    markedScope(node) == scope && node.description == device.name && node.mediaClass == mediaClass;
                if (right && (newest == nullptr || node.object.serial > newest->object.serial)) {
                    newest = &node;
                }
            }

            if (newest == nullptr) {
                // Nothing of the right name and description: never made, or
                // renamed. The old node, if any, stays until the new one is
                // ready to take its streams over.
                //
                // **A rename under way is finished by any pass**, not only a
                // full one. A second rename that arrives while the first's
                // node is on its way waits for it, and the pass that sees it
                // arrive is a light one; without this, the device would stay
                // on the first new name. A node of ours with the old name is
                // what says a rename is under way, so a device somebody else
                // removed is still not made again behind their back.
                const bool renaming =
                    std::ranges::any_of(sameName, [](const PwNode &node) { return markedScope(node).has_value(); });
                if ((full || renaming) && !pending.contains(name) && !refused.contains(device.id)) {
                    create(device, sameName);
                }
                continue;
            }
            // **Only once the new node has its ports** does the old one go:
            // until then WirePlumber has nowhere to move the streams but the
            // default device.
            if (mirror.portsOf(newest->object.id, PortDirection::Input).isEmpty()) {
                continue;
            }
            for (const PwNode &node : sameName) {
                if (node.object.id != newest->object.id && markedScope(node)) {
                    destroy(node);
                }
            }
        }
    }

    if (full) {
        // A marked node no enabled entry accounts for — deleted, disabled,
        // or left over from a file edited while PipeWire ran — goes, unless
        // its scope's file could not be read.
        for (const PwNode &node : nodes) {
            const std::optional<VirtualDeviceScope> scope = markedScope(node);
            if (!scope || loadErrors[indexOf(*scope)] || destroying.contains(node.object.id)) {
                continue;
            }
            if (!wanted[indexOf(*scope)].contains(node.name) && !pending.contains(node.name)) {
                destroy(node);
            }
        }
    }
    announce();
}

void VirtualDeviceManager::Private::create(const VirtualDevice &device, const QList<PwNode> &sameName)
{
    const QString name = virtualDeviceNodeName(device.id);
    // **PipeWire does not refuse a second node of a name already in use**
    // (the spike, §3). A node of ours of that name is one a rename replaces;
    // one that is not ours is somebody else's, and a second beside it would
    // only confuse every list it appears in.
    if (std::ranges::any_of(sameName, [](const PwNode &node) { return !markedScope(node); })) {
        qCWarning(RB_PW) << "not making virtual device" << name << ": a node of that name is already there";
        return;
    }

    pw_properties *props = pw_properties_new(PW_KEY_OBJECT_LINGER, "true", nullptr);
    for (const auto &[key, value] : virtualDeviceNodeProperties(device, device.scope)) {
        pw_properties_set(props, key.constData(), value.constData());
    }
    auto *proxy = static_cast<pw_proxy *>(
        pw_core_create_object(connection.core(), "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0));
    pw_properties_free(props);

    if (proxy == nullptr) {
        refused.insert(device.id);
        Q_EMIT q->nodeRefused(device.id, u"the adapter factory made nothing"_s);
        return;
    }
    auto request = std::make_unique<PendingNode>();
    request->owner = this;
    request->deviceId = device.id;
    request->nodeName = name;
    for (const PwNode &node : sameName) {
        request->before.insert(node.object.serial);
    }
    request->proxy = proxy;
    pw_proxy_add_listener(proxy, &request->listener, &pendingProxyEvents(), request.get());
    request->listening = true;
    pending.emplace(name, std::move(request));
}

void VirtualDeviceManager::Private::destroy(const PwNode &node)
{
    if (pw_registry *registry = connection.registry(); registry != nullptr) {
        pw_registry_destroy(registry, node.object.id);
        destroying.insert(node.object.id);
    }
}

void VirtualDeviceManager::Private::completePending(const QList<PwNode> &nodes)
{
    for (auto request = pending.begin(); request != pending.end();) {
        const bool arrived = std::ranges::any_of(nodes, [&](const PwNode &node) {
            return node.name == request->second->nodeName && !request->second->before.contains(node.object.serial);
        });
        if (arrived) {
            retire(std::move(request->second));
            request = pending.erase(request);
        } else {
            ++request;
        }
    }
}

void VirtualDeviceManager::Private::refuse(PendingNode *request, const QString &detail)
{
    qCWarning(RB_PW) << "PipeWire would not make virtual device" << request->nodeName << ":" << detail;
    const QString deviceId = request->deviceId;
    const auto found = pending.find(request->nodeName);
    if (found != pending.end() && found->second.get() == request) {
        retire(std::move(found->second));
        pending.erase(found);
    }
    refused.insert(deviceId);
    Q_EMIT q->nodeRefused(deviceId, detail);
    announce();
}

void VirtualDeviceManager::Private::retire(std::unique_ptr<PendingNode> request)
{
    // The request may be the one whose callback is running; a proxy cannot
    // be destroyed from inside its own event.
    retired.push_back(std::move(request));
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

void VirtualDeviceManager::Private::forgetGraph()
{
    // The connection is going. The proxies still belong to it, so they are
    // let go of now; the nodes stay, lingering, as they are meant to.
    retired.clear();
    pending.clear();
    destroying.clear();
    refused.clear();
}

VirtualDeviceManager::VirtualDeviceManager(Connection &connection, RegistryMirror &mirror, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, connection, mirror))
{
    d->timer = new QTimer(this);
    d->timer->setSingleShot(true);
    d->timer->setInterval(0);
    connect(d->timer, &QTimer::timeout, this, [this] { d->runScheduled(); });

    d->load();
    d->announced = d->states();

    connect(&mirror, &RegistryMirror::ready, this, [this] {
        d->refused.clear();
        d->schedule(true);
    });
    connect(&mirror, &RegistryMirror::cleared, this, [this] {
        d->forgetGraph();
        d->announce();
    });
    const auto onChange = [this] { d->schedule(false); };
    connect(&mirror, &RegistryMirror::nodeAdded, this, onChange);
    connect(&mirror, &RegistryMirror::nodeChanged, this, onChange);
    connect(&mirror, &RegistryMirror::portAdded, this, onChange);
    connect(&mirror, &RegistryMirror::nodeRemoved, this, [this](std::uint32_t id, std::uint64_t) {
        d->destroying.erase(id);
        d->schedule(false);
    });
}

VirtualDeviceManager::~VirtualDeviceManager()
{
    d->forgetGraph();
}

void VirtualDeviceManager::setStores(VirtualDeviceStore user, VirtualDeviceStore system)
{
    d->stores[indexOf(VirtualDeviceScope::User)] = std::move(user);
    d->stores[indexOf(VirtualDeviceScope::System)] = std::move(system);
    reload();
}

const VirtualDeviceStore &VirtualDeviceManager::store(VirtualDeviceScope scope) const
{
    return d->stores[indexOf(scope)];
}

QList<VirtualDeviceState> VirtualDeviceManager::devices() const
{
    return d->states();
}

std::optional<VirtualDeviceState> VirtualDeviceManager::device(const QString &id) const
{
    for (const VirtualDeviceState &state : d->states()) {
        if (state.device.id == id) {
            return state;
        }
    }
    return std::nullopt;
}

std::optional<VirtualDeviceError> VirtualDeviceManager::scopeError(VirtualDeviceScope scope) const
{
    return d->loadErrors[indexOf(scope)];
}

QString VirtualDeviceManager::newId() const
{
    return newVirtualDeviceId(d->lists[0] + d->lists[1]);
}

std::expected<VirtualDevice, VirtualDeviceError>
VirtualDeviceManager::create(const QString &name, VirtualDeviceDirection direction, VirtualDeviceChannels channels)
{
    const std::expected<QString, VirtualDeviceError> valid = validVirtualDeviceName(name);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    VirtualDevice device;
    device.id = newId();
    device.name = *valid;
    device.direction = direction;
    device.channels = channels;
    device.enabled = true;
    device.scope = VirtualDeviceScope::User;

    QList<VirtualDevice> changed = d->lists[indexOf(VirtualDeviceScope::User)];
    changed.append(device);
    if (std::expected<void, VirtualDeviceError> saved = d->saveUser(std::move(changed)); !saved) {
        return std::unexpected(saved.error());
    }
    d->schedule(true);
    d->announce();
    return device;
}

std::expected<void, VirtualDeviceError> VirtualDeviceManager::rename(const QString &id, const QString &name)
{
    const VirtualDevice *device = d->find(id);
    if (device == nullptr) {
        return std::unexpected(VirtualDeviceError::NotFound);
    }
    if (device->scope != VirtualDeviceScope::User) {
        return std::unexpected(VirtualDeviceError::ReadOnlyScope);
    }
    const std::expected<QString, VirtualDeviceError> valid = validVirtualDeviceName(name);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    if (device->name == *valid) {
        return {};
    }
    QList<VirtualDevice> changed = d->lists[indexOf(VirtualDeviceScope::User)];
    for (VirtualDevice &entry : changed) {
        if (entry.id == id) {
            entry.name = *valid;
        }
    }
    if (std::expected<void, VirtualDeviceError> saved = d->saveUser(std::move(changed)); !saved) {
        return saved;
    }
    d->refused.remove(id);
    d->schedule(true);
    d->announce();
    return {};
}

std::expected<void, VirtualDeviceError> VirtualDeviceManager::setEnabled(const QString &id, bool enabled)
{
    const VirtualDevice *device = d->find(id);
    if (device == nullptr) {
        return std::unexpected(VirtualDeviceError::NotFound);
    }
    if (device->scope != VirtualDeviceScope::User) {
        return std::unexpected(VirtualDeviceError::ReadOnlyScope);
    }
    if (device->enabled == enabled) {
        return {};
    }
    QList<VirtualDevice> changed = d->lists[indexOf(VirtualDeviceScope::User)];
    for (VirtualDevice &entry : changed) {
        if (entry.id == id) {
            entry.enabled = enabled;
        }
    }
    if (std::expected<void, VirtualDeviceError> saved = d->saveUser(std::move(changed)); !saved) {
        return saved;
    }
    d->refused.remove(id);
    d->schedule(true);
    d->announce();
    return {};
}

std::expected<void, VirtualDeviceError> VirtualDeviceManager::remove(const QString &id)
{
    const VirtualDevice *device = d->find(id);
    if (device == nullptr) {
        return std::unexpected(VirtualDeviceError::NotFound);
    }
    if (device->scope != VirtualDeviceScope::User) {
        return std::unexpected(VirtualDeviceError::ReadOnlyScope);
    }
    QList<VirtualDevice> changed = d->lists[indexOf(VirtualDeviceScope::User)];
    changed.removeIf([&](const VirtualDevice &entry) { return entry.id == id; });
    if (std::expected<void, VirtualDeviceError> saved = d->saveUser(std::move(changed)); !saved) {
        return saved;
    }
    d->refused.remove(id);
    d->schedule(true);
    d->announce();
    return {};
}

void VirtualDeviceManager::reload()
{
    d->load();
    d->refused.clear();
    d->schedule(true);
    d->announce();
}

void VirtualDeviceManager::reconcileNow()
{
    d->timer->stop();
    d->fullPassWanted = false;
    d->reconcile(true);
}

} // namespace rb::pw
