// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "recentapplications.h"

#include "appshell.h"

#include <KConfigGroup>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

KSharedConfig::Ptr g_config;
std::function<QDateTime()> g_clock;

constexpr QLatin1StringView kGroup{"RecentApplications"};
/// The entries are numbered rather than keyed by the rule itself. **A match
/// rule is not a configuration key**: it carries a colon and whatever
/// characters a program's name happens to have, and numbering sidesteps
/// every question about which of those KConfig would have to escape.
constexpr QLatin1StringView kEntryPrefix{"Application"};

/// A rule that names one stream on one boot. Remembering it would be
/// remembering nothing: the id it stands for belongs to a process that has
/// already gone.
constexpr QLatin1StringView kNodePrefix{"node:"};

[[nodiscard]] QDateTime now()
{
    return g_clock ? g_clock() : QDateTime::currentDateTimeUtc();
}

[[nodiscard]] KConfigGroup group()
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    return KConfigGroup(config, kGroup);
}

struct Seen {
    QString key;
    QString name;
    QDateTime lastSeen;
};

} // namespace

class RecentApplications::Private
{
public:
    QPointer<core::EngineFacade> facade;
    QList<Seen> seen;

    [[nodiscard]] qsizetype indexOf(const QString &key) const
    {
        for (qsizetype index = 0; index < seen.size(); ++index) {
            if (seen.at(index).key == key) {
                return index;
            }
        }
        return -1;
    }

    /// Reads the file, dropping whatever has gone stale or does not parse.
    void load()
    {
        seen.clear();
        const KConfigGroup state = group();
        const QDateTime oldest = now().addDays(-kRememberedDays);
        for (int index = 1; index <= kMaxEntries; ++index) {
            const QStringList fields = state.readEntry(kEntryPrefix + QString::number(index), QStringList{});
            if (fields.size() != 3) {
                continue;
            }
            Seen entry;
            entry.key = fields.at(0);
            entry.name = fields.at(1);
            entry.lastSeen = QDateTime::fromString(fields.at(2), Qt::ISODate);
            if (entry.key.isEmpty() || entry.key.startsWith(kNodePrefix) || !entry.lastSeen.isValid()
                || entry.lastSeen < oldest) {
                continue;
            }
            seen.append(entry);
        }
        sort();
    }

    void sort()
    {
        std::stable_sort(seen.begin(), seen.end(), [](const Seen &left, const Seen &right) {
            return left.lastSeen > right.lastSeen;
        });
        if (seen.size() > kMaxEntries) {
            seen.resize(kMaxEntries);
        }
    }

    void save() const
    {
        KConfigGroup state = group();
        // **The whole group is rewritten**, because the entries are
        // numbered and their order is the answer: an entry left behind
        // under a number nobody wrote this time would be read back as one
        // of the list.
        state.deleteGroup();
        KConfigGroup fresh = group();
        for (qsizetype index = 0; index < seen.size(); ++index) {
            const Seen &entry = seen.at(index);
            fresh.writeEntry(kEntryPrefix + QString::number(index + 1),
                             QStringList{entry.key, entry.name, entry.lastSeen.toString(Qt::ISODate)});
        }
        fresh.sync();
    }

    /// Notes one sighting. Says whether the file has to be written: a new
    /// application, a program that has been renamed, or a time that has
    /// grown stale enough to matter.
    [[nodiscard]] bool note(const QString &key, const QString &name, const QDateTime &when)
    {
        if (key.isEmpty() || key.startsWith(kNodePrefix)) {
            return false;
        }
        const qsizetype found = indexOf(key);
        if (found < 0) {
            seen.append(Seen{key, name, when});
            sort();
            return true;
        }
        Seen &entry = seen[found];
        const bool worthWriting = entry.name != name || entry.lastSeen.addSecs(kRewriteAfterMinutes * 60) < when;
        entry.name = name;
        entry.lastSeen = when;
        sort();
        return worthWriting;
    }
};

RecentApplications::RecentApplications(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->load();
    // **Attached here rather than only in create()**, for the reason the
    // inventory gives: whether QML reaches a singleton through its create
    // function is Qt's business and has changed before.
    attach(AppShell::engine());
}

RecentApplications::~RecentApplications() = default;

RecentApplications *RecentApplications::create(QQmlEngine *, QJSEngine *)
{
    return new RecentApplications;
}

void RecentApplications::setConfig(const KSharedConfig::Ptr &config)
{
    g_config = config;
}

void RecentApplications::setClock(std::function<QDateTime()> clock)
{
    g_clock = std::move(clock);
}

void RecentApplications::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade == nullptr) {
        return;
    }
    connect(facade, &core::EngineFacade::inventoryChanged, this, [this](const QString &inventoryJson) {
        const QJsonArray applications =
            QJsonDocument::fromJson(inventoryJson.toUtf8()).object().value("applications"_L1).toArray();
        const QDateTime when = now();
        bool write = false;
        for (const QJsonValue &application : applications) {
            const QJsonObject entry = application.toObject();
            write = d->note(entry.value("key"_L1).toString(), entry.value("name"_L1).toString(), when) || write;
        }
        if (write) {
            d->save();
        }
        Q_EMIT changed();
    });
    facade->getInventory();
}

QVariantList RecentApplications::entries() const
{
    QVariantList entries;
    entries.reserve(d->seen.size());
    for (const Seen &entry : std::as_const(d->seen)) {
        entries.append(QVariantMap{
            {"key"_L1, entry.key},
            {"name"_L1, entry.name},
            {"lastSeen"_L1, entry.lastSeen},
        });
    }
    return entries;
}

void RecentApplications::remember(const QString &key, const QString &name)
{
    if (!d->note(key, name, now())) {
        // Nothing changed that is worth a write, but the order may have,
        // and a picker that is open is looking at it.
        Q_EMIT changed();
        return;
    }
    d->save();
    Q_EMIT changed();
}

bool RecentApplications::knows(const QString &key) const
{
    return d->indexOf(key) >= 0;
}

QString RecentApplications::nameFor(const QString &key) const
{
    const qsizetype found = d->indexOf(key);
    return found < 0 ? QString() : d->seen.at(found).name;
}

} // namespace rb::ui
