// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "installedplugins.h"

#include "appshell.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::ui
{

class InstalledPlugins::Private
{
public:
    QPointer<core::EngineFacade> facade;
    bool ready = false;
    bool scanning = false;
    QVariantList plugins;
    QVariantList blocked;
    QVariantList incomplete;
    QStringList formats;
    /// What each plugin's settings are, by `format:id`, as the engine said.
    QHash<QString, QVariantList> params;
    QSet<QString> asked;
};

InstalledPlugins::InstalledPlugins(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // Attached here as well as in create(), for the reason the block
    // catalogue is: however QML makes the singleton, it has an engine.
    attach(AppShell::engine());
}

InstalledPlugins::~InstalledPlugins() = default;

InstalledPlugins *InstalledPlugins::create(QQmlEngine *, QJSEngine *)
{
    return new InstalledPlugins;
}

void InstalledPlugins::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade == nullptr) {
        return;
    }
    connect(facade, &core::EngineFacade::pluginsChanged, this, [this](const QString &json) {
        setPlugins(QJsonDocument::fromJson(json.toUtf8()).object());
    });
    connect(facade,
            &core::EngineFacade::pluginDescribed,
            this,
            [this](const QString &format, const QString &id, const QString &json) {
                d->params.insert(
                    format + u':' + id,
                    QJsonDocument::fromJson(json.toUtf8()).object().value("params"_L1).toArray().toVariantList());
                Q_EMIT changed();
            });
    facade->listPlugins();
}

void InstalledPlugins::setPlugins(const QJsonObject &json)
{
    // A scan may have found a plugin anew: its settings are asked again.
    if (!json.value("scanning"_L1).toBool()) {
        d->params.clear();
        d->asked.clear();
    }
    d->scanning = json.value("scanning"_L1).toBool();
    d->plugins.clear();
    d->formats.clear();
    for (const QJsonValue &entry : json.value("plugins"_L1).toArray()) {
        QVariantMap plugin = entry.toObject().toVariantMap();
        const QString format = plugin.value(u"format"_s).toString();
        plugin.insert(u"key"_s, entryKey(format, plugin.value(u"id"_s).toString()));
        plugin.insert(u"type"_s, blockType(format));
        if (!d->formats.contains(format)) {
            d->formats.append(format);
        }
        d->plugins.append(plugin);
    }
    d->blocked = json.value("blocked"_L1).toArray().toVariantList();
    d->incomplete = json.value("incomplete"_L1).toArray().toVariantList();
    d->ready = true;
    Q_EMIT changed();
}

bool InstalledPlugins::isReady() const
{
    return d->ready;
}

bool InstalledPlugins::isScanning() const
{
    return d->scanning;
}

QVariantList InstalledPlugins::plugins() const
{
    return d->plugins;
}

QVariantList InstalledPlugins::blocked() const
{
    return d->blocked;
}

QVariantList InstalledPlugins::incomplete() const
{
    return d->incomplete;
}

QStringList InstalledPlugins::formats() const
{
    return d->formats;
}

QString InstalledPlugins::blockType(const QString &format)
{
    return u"plugin."_s + format;
}

QString InstalledPlugins::entryKey(const QString &format, const QString &id)
{
    return blockType(format) + u"?plugin="_s + QString::fromLatin1(QUrl::toPercentEncoding(id));
}

QVariantMap InstalledPlugins::plugin(const QString &format, const QString &id) const
{
    for (const QVariant &entry : d->plugins) {
        const QVariantMap plugin = entry.toMap();
        if (plugin.value(u"format"_s).toString() == format && plugin.value(u"id"_s).toString() == id) {
            return plugin;
        }
    }
    return {};
}

QVariantList InstalledPlugins::params(const QString &format, const QString &id)
{
    const QString key = format + u':' + id;
    if (const auto found = d->params.constFind(key); found != d->params.constEnd()) {
        return *found;
    }
    if (d->facade && !id.isEmpty() && !d->asked.contains(key)) {
        d->asked.insert(key);
        // **Asked after the binding that wanted it has finished**: an
        // engine that answers at once would otherwise change what that
        // binding is still reading.
        QMetaObject::invokeMethod(
            this,
            [this, format, id] {
                if (d->facade) {
                    d->facade->describePlugin(format, id);
                }
            },
            Qt::QueuedConnection);
    }
    return {};
}

QVariantList InstalledPlugins::pluginsOf(const QString &format) const
{
    QVariantList found;
    for (const QVariant &entry : d->plugins) {
        if (entry.toMap().value(u"format"_s).toString() == format) {
            found.append(entry);
        }
    }
    // By name, as a person looks for one.
    std::ranges::sort(found, [](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(a.toMap().value(u"name"_s).toString(), b.toMap().value(u"name"_s).toString())
            < 0;
    });
    return found;
}

void InstalledPlugins::rescan(bool retryBlocked)
{
    if (d->facade) {
        d->facade->rescanPlugins(retryBlocked);
    }
}

} // namespace rb::ui
