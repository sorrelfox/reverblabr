// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "plugin/formats.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace rb::core
{

/// One plugin installed on this machine, as the last scan found it.
struct InstalledPlugin {
    QString format;
    QString id;
    QString name;
    QString vendor;
    QString category;
    int audioInputs = 0;
    int audioOutputs = 0;
    int parameters = 0;
    /// The bundle, file or library it was found in.
    QString candidate;
    /// Its settings, as the scan wrote them: id, name, kind, min,
    /// max, default, log and, for a choice, choices of value and label.
    QJsonArray params;

    [[nodiscard]] bool operator==(const InstalledPlugin &other) const = default;
};

/// A candidate a scan could not look into, and will not again until it
/// changes or a rescan asks it to.
struct BlockedCandidate {
    QString format;
    QString path;
    /// "crashed" or "hung"; "incomplete" in incomplete().
    QString reason;

    [[nodiscard]] bool operator==(const BlockedCandidate &other) const = default;
};

/// What ListPlugins answers (the interface XML): the plugins, the
/// blocklisted candidates, whether a scan is under way, and the candidates
/// whose describing stopped part of the way.
[[nodiscard]] QJsonObject pluginsJson(const QList<InstalledPlugin> &plugins,
                                      const QList<BlockedCandidate> &blocked,
                                      bool scanning,
                                      const QList<BlockedCandidate> &incomplete = {});

/// A plugin's settings as a scan wrote them, read back.
[[nodiscard]] std::vector<plugin::PluginParam> paramsFromJson(const QJsonArray &json);

/// Where the engine keeps its plugin cache.
[[nodiscard]] QString defaultPluginCachePath();

/// **Finds the plugins installed on this machine, and remembers them**.
///
/// Every format lists its candidates by name alone (plugin::formats()), and
/// each candidate is described by a `reverblabr-plugin-host --scan` of its
/// own, a few at a time: **a plugin that crashes or hangs on being looked at
/// takes only that process with it**, and the candidate is blocklisted —
/// never looked into again until it changes, or somebody asks for a retry.
///
/// **A scan looks only at what changed.** What was found is kept in an
/// SQLite cache, with each candidate's modification time; a candidate whose
/// time has not moved is not described again, one that has gone takes its
/// plugins with it, and a new one is described. The folders the formats
/// look in are watched, and a change in one scans again.
///
/// Lives on the engine thread.
class PluginScanner : public QObject
{
    Q_OBJECT

public:
    /// Opens (and if need be makes) the cache at \a cachePath.
    explicit PluginScanner(const QString &cachePath, QObject *parent = nullptr);
    ~PluginScanner() override;

    /// The host that describes, and how long it may go without describing
    /// another plugin of a candidate before it is given up on: hung
    /// if it had described none, incomplete otherwise. Tests shorten it.
    void setHostProgram(const QString &program);
    void setTimeout(std::chrono::milliseconds timeout);
    /// How many candidates are described at once.
    void setParallel(int processes);
    /// Scans again when a folder the formats look in changes. On by default.
    void setWatchingFolders(bool watching);

    /// **Scans for what has changed since the last scan.** \a retryBlocked
    /// looks into the blocklisted candidates again too. A scan asked for
    /// while one is running follows it.
    void scan(bool retryBlocked = false);
    [[nodiscard]] bool isScanning() const;

    /// Every plugin found, by format and then by name.
    [[nodiscard]] QList<InstalledPlugin> plugins() const;
    [[nodiscard]] QList<BlockedCandidate> blocked() const;
    /// **Candidates a scan got only part of the way through**: what
    /// they told is among plugins(), with what an earlier scan found in them,
    /// and the next scan looks into them again.
    [[nodiscard]] QList<BlockedCandidate> incomplete() const;

    /// **What one plugin is**, from the cache: what a plugin block
    /// builds its parameters from. Nothing for a plugin no scan has found.
    [[nodiscard]] std::optional<plugin::PluginDescription> describe(const QString &format, const QString &id) const;
    /// The same plugin's settings as the scan wrote them, for the facade.
    [[nodiscard]] QJsonArray paramsJson(const QString &format, const QString &id) const;

    /// How many candidates the last scan described: what shows that a scan
    /// with nothing changed looked into nothing.
    [[nodiscard]] int lastScanDescribed() const;

Q_SIGNALS:
    void scanningChanged(bool scanning);
    /// What plugins() or blocked() answer has changed.
    void changed();

public:
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::core
