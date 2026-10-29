// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "inventory.h"

#include "appshell.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

#include <array>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// One list out of the inventory, as it arrived, and where to find an entry
/// by the field that identifies it without walking the list.
///
/// **What identifies an entry differs by list**: a device is its
/// `node.name`, an application is its match rule, so the field is named
/// once here rather than being a second class per list.
class KeyedList
{
public:
    explicit KeyedList(QLatin1StringView key)
        : m_key(key)
    {
    }

    void read(const QJsonArray &entries)
    {
        m_entries.clear();
        m_byKey.clear();
        for (const QJsonValue &entry : entries) {
            const QVariantMap item = entry.toObject().toVariantMap();
            const QString key = item.value(m_key).toString();
            if (key.isEmpty()) {
                continue;
            }
            m_byKey.insert(key, m_entries.size());
            m_entries.append(item);
        }
    }

    [[nodiscard]] const QVariantList &all() const
    {
        return m_entries;
    }

    [[nodiscard]] bool has(const QString &key) const
    {
        return m_byKey.contains(key);
    }

    [[nodiscard]] QVariantMap find(const QString &key) const
    {
        const auto found = m_byKey.constFind(key);
        return found == m_byKey.constEnd() ? QVariantMap{} : m_entries.at(*found).toMap();
    }

private:
    QLatin1StringView m_key;
    QVariantList m_entries;
    QHash<QString, qsizetype> m_byKey;
};

/// The prefixes a match rule can carry. A picker may be handed one of these
/// whole — pasted, or read out of another session — and it is then kept as
/// it stands rather than being wrapped in a second rule.
constexpr QLatin1StringView kBinaryPrefix{"binary:"};
constexpr QLatin1StringView kFlatpakPrefix{"flatpak:"};
constexpr QLatin1StringView kNamePrefix{"name:"};
constexpr QLatin1StringView kNodePrefix{"node:"};

constexpr std::array<QLatin1StringView, 4> kRulePrefixes{
    kBinaryPrefix,
    kFlatpakPrefix,
    kNamePrefix,
    kNodePrefix,
};

/// The three fields a session stores an application by, always all three:
/// a block pointed at something new has to stop matching what it was
/// pointed at before, and a field nobody wrote would go on matching.
[[nodiscard]] QVariantMap noApplication()
{
    return QVariantMap{
        {"binary"_L1, QString{}},
        {"flatpakId"_L1, QString{}},
        {"name"_L1, QString{}},
    };
}

} // namespace

class Inventory::Private
{
public:
    QPointer<core::EngineFacade> facade;
    bool ready = false;
    qulonglong revision = 0;
    KeyedList outputs{"node"_L1};
    KeyedList inputs{"node"_L1};
    KeyedList applications{"key"_L1};
    QString defaultOutput;
    QString defaultInput;

    /// The node a reference points at: a name is itself, and kDefaultDevice
    /// is whatever the system has made the default.
    [[nodiscard]] QString resolve(const QString &node, const QString &fallback) const
    {
        return node == kDefaultDevice ? fallback : node;
    }
};

Inventory::Inventory(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // **Attached here rather than only in create()**, for the reason the
    // block catalogue gives: whether QML reaches a singleton through its
    // create function is Qt's business and has changed before, and a picker
    // that quietly has no engine is a picker that quietly has no devices.
    attach(AppShell::engine());
}

Inventory::~Inventory() = default;

Inventory *Inventory::create(QQmlEngine *, QJSEngine *)
{
    return new Inventory;
}

void Inventory::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade == nullptr) {
        return;
    }
    connect(facade, &core::EngineFacade::inventoryChanged, this, [this](const QString &inventoryJson) {
        setInventory(QJsonDocument::fromJson(inventoryJson.toUtf8()).object());
    });
    // **Asked for once, and kept current by the signal.** The engine sends
    // the whole inventory whenever a list changes, so there is never a
    // reason to ask again.
    facade->getInventory();
}

void Inventory::setInventory(const QJsonObject &inventory)
{
    d->outputs.read(inventory.value("outputs"_L1).toArray());
    d->inputs.read(inventory.value("inputs"_L1).toArray());
    d->applications.read(inventory.value("applications"_L1).toArray());
    const QJsonObject defaults = inventory.value("defaults"_L1).toObject();
    d->defaultOutput = defaults.value("sink"_L1).toString();
    d->defaultInput = defaults.value("source"_L1).toString();
    d->ready = true;
    ++d->revision;
    Q_EMIT changed();
}

bool Inventory::isReady() const
{
    return d->ready;
}

qulonglong Inventory::revision() const
{
    return d->revision;
}

QVariantList Inventory::outputs() const
{
    return d->outputs.all();
}

QVariantList Inventory::inputs() const
{
    return d->inputs.all();
}

QVariantList Inventory::applications() const
{
    return d->applications.all();
}

QString Inventory::defaultOutput() const
{
    return d->defaultOutput;
}

QString Inventory::defaultInput() const
{
    return d->defaultInput;
}

QString Inventory::defaultDevice() const
{
    return QString(kDefaultDevice);
}

QVariantMap Inventory::application(const QString &key) const
{
    return d->applications.find(key);
}

bool Inventory::hasApplication(const QString &key) const
{
    return d->applications.has(key);
}

QString Inventory::applicationName(const QString &key) const
{
    const QString name = d->applications.find(key).value("name"_L1).toString();
    if (!name.isEmpty()) {
        return name;
    }
    // Nothing of that application is playing, so nobody can say what it
    // calls itself. The rule's own subject is the next best thing, and it
    // is what somebody typed in the first place.
    const qsizetype colon = key.indexOf(u':');
    return colon < 0 ? key : key.sliced(colon + 1);
}

QString Inventory::applicationRule(const QString &text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    for (const QLatin1StringView prefix : kRulePrefixes) {
        if (trimmed.startsWith(prefix)) {
            return trimmed;
        }
    }
    return kBinaryPrefix + trimmed;
}

QString Inventory::applicationRuleFrom(const QString &binary, const QString &flatpakId, const QString &name) const
{
    // **The engine's own order**, which is core::SessionPlan's: every field
    // that is set has to match, so the plan keeps the first one it finds
    // and clears the rest. A rule built in any other order would name an
    // application the engine is not looking for.
    if (!binary.isEmpty()) {
        return kBinaryPrefix + binary;
    }
    if (!flatpakId.isEmpty()) {
        return kFlatpakPrefix + flatpakId;
    }
    if (!name.isEmpty()) {
        return kNamePrefix + name;
    }
    return QString();
}

QVariantMap Inventory::applicationFields(const QString &rule) const
{
    // Through applicationRule() first, so that a bare program name — which
    // is what somebody types — arrives here as the rule it means.
    const QString full = applicationRule(rule);
    QVariantMap fields = noApplication();
    if (full.startsWith(kBinaryPrefix)) {
        fields.insert("binary"_L1, full.sliced(kBinaryPrefix.size()));
    } else if (full.startsWith(kFlatpakPrefix)) {
        fields.insert("flatpakId"_L1, full.sliced(kFlatpakPrefix.size()));
    } else if (full.startsWith(kNamePrefix)) {
        fields.insert("name"_L1, full.sliced(kNamePrefix.size()));
    }
    // Anything else — a node-scoped rule, or nothing at all — leaves all
    // three empty, which is a block that is pointed at nothing rather than
    // one pointed at everything.
    return fields;
}

QVariantMap Inventory::output(const QString &node) const
{
    return d->outputs.find(d->resolve(node, d->defaultOutput));
}

QVariantMap Inventory::input(const QString &node) const
{
    return d->inputs.find(d->resolve(node, d->defaultInput));
}

bool Inventory::hasOutput(const QString &node) const
{
    return d->outputs.has(d->resolve(node, d->defaultOutput));
}

bool Inventory::hasInput(const QString &node) const
{
    return d->inputs.has(d->resolve(node, d->defaultInput));
}

QString Inventory::outputName(const QString &node) const
{
    const QString resolved = d->resolve(node, d->defaultOutput);
    const QString description = d->outputs.find(resolved).value("description"_L1).toString();
    return description.isEmpty() ? resolved : description;
}

QString Inventory::inputName(const QString &node) const
{
    const QString resolved = d->resolve(node, d->defaultInput);
    const QString description = d->inputs.find(resolved).value("description"_L1).toString();
    return description.isEmpty() ? resolved : description;
}

QStringList Inventory::outputChannels(const QString &node) const
{
    return d->outputs.find(d->resolve(node, d->defaultOutput)).value("channels"_L1).toStringList();
}

QStringList Inventory::inputChannels(const QString &node) const
{
    return d->inputs.find(d->resolve(node, d->defaultInput)).value("channels"_L1).toStringList();
}

} // namespace rb::ui
