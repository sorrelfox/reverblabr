// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// **The plugins installed on this machine**, as the library shows them.
///
/// The engine scans for them and keeps what it found; this follows what it
/// says (PluginsChanged). **It knows no format**: a plugin is a format, an
/// id, a name and a vendor, and the block it becomes is `plugin.<format>`
/// with its "plugin" setting the id — so a format the host learns to load
/// is in the library with nothing added here.
///
/// **A library entry for a plugin is a key**, `plugin.<format>?plugin=<id>`,
/// carried where a block's type id is carried: dragged to the canvas, or
/// chosen. SessionModel::addBlock() reads it back into a type and a
/// setting, so the canvas and the library need nothing of their own for it.
class InstalledPlugins : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// Whether the engine has answered yet.
    Q_PROPERTY(bool ready READ isReady NOTIFY changed)
    /// Whether a scan is under way.
    Q_PROPERTY(bool scanning READ isScanning NOTIFY changed)
    /// Every plugin: format, id, name, vendor, category, audioInputs,
    /// audioOutputs, parameters, and the entry key. By format, then name.
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY changed)
    /// The candidates a scan blocklisted: format, path, reason.
    Q_PROPERTY(QVariantList blocked READ blocked NOTIFY changed)
    /// The candidates a scan got only part of the way through:
    /// format, path. Some of their plugins may be missing.
    Q_PROPERTY(QVariantList incomplete READ incomplete NOTIFY changed)
    /// The formats there are plugins of, in the order they were found.
    Q_PROPERTY(QStringList formats READ formats NOTIFY changed)

public:
    explicit InstalledPlugins(QObject *parent = nullptr);
    ~InstalledPlugins() override;

    static InstalledPlugins *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade, asking it at once. A test gives its own.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool isScanning() const;
    [[nodiscard]] QVariantList plugins() const;
    [[nodiscard]] QVariantList blocked() const;
    [[nodiscard]] QVariantList incomplete() const;
    [[nodiscard]] QStringList formats() const;

    /// The block type a plugin of \a format becomes.
    [[nodiscard]] Q_INVOKABLE static QString blockType(const QString &format);
    /// The library key for a plugin: where a type id travels, it travels.
    [[nodiscard]] Q_INVOKABLE static QString entryKey(const QString &format, const QString &id);
    /// The plugin of \a format called \a id, or an empty map.
    [[nodiscard]] Q_INVOKABLE QVariantMap plugin(const QString &format, const QString &id) const;

    /// **One plugin's settings**: id, name, kind, min, max,
    /// default, log, and a choice's choices of value and label, in the
    /// plugin's order. Asked of the engine the first time; empty until it
    /// has answered, and changed() says when it has.
    [[nodiscard]] Q_INVOKABLE QVariantList params(const QString &format, const QString &id);

    /// The plugins of \a format, by name: what a plugin block's chooser
    /// offers.
    [[nodiscard]] Q_INVOKABLE QVariantList pluginsOf(const QString &format) const;

    /// Scans for what has changed; \a retryBlocked looks into the
    /// blocklisted candidates again too.
    Q_INVOKABLE void rescan(bool retryBlocked = false);

    /// Reads a list that did not come from the engine. **For tests.**
    void setPlugins(const QJsonObject &json);

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
