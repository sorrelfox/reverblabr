// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sessions.h"

#include "appshell.h"
#include "core/editcommand.h"
#include "core/session.h"
#include "core/sessionstore.h"

#include <KConfigGroup>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

KSharedConfig::Ptr g_config;

constexpr QLatin1StringView kGroup{"Sessions"};
constexpr QLatin1StringView kOpen{"Open"};
constexpr QLatin1StringView kCurrent{"Current"};
/// What each open session was called, by id, so that one whose file has
/// gone can still be named.
constexpr QLatin1StringView kNames{"Names"};

/// One row as QML reads it. **The state is in the row**, because a tab
/// shows a live dot for its own session and AppShell only knows about the
/// one in front.
[[nodiscard]] QVariantMap row(const QString &id, const QString &name, const QString &state)
{
    return QVariantMap{
        {u"id"_s, id},
        {u"name"_s, name},
        {u"state"_s, state},
        {u"running"_s, state == QLatin1StringView(core::sessionState::running)},
    };
}

} // namespace

class Sessions::Private
{
public:
    QPointer<core::EngineFacade> facade;

    /// Every session on disk, newest answer from the engine.
    QList<QPair<QString, QString>> saved;
    bool listed = false;
    /// The open ones, in tab order.
    QStringList openIds;
    /// What each open session is called. Kept apart from `saved` because a
    /// session is renamed while it is open and the list is not asked again
    /// for every keystroke.
    QHash<QString, QString> names;
    QString currentId;
    /// What each open session is doing, so a tab can show its own live dot
    /// rather than the front session's.
    QHash<QString, QString> states;

    /// The ids restore() is still waiting for, so that a session opening
    /// because somebody asked for it is not mistaken for one coming back.
    QStringList awaited;
    /// Whether a restore is in progress. **Not just "awaited is empty"**: a
    /// restore whose every file has gone has nothing left to wait for and
    /// still has to end with a session to show.
    bool restoring = false;
    /// What to call the session a restore makes when nothing came back.
    QString untitledName;
    /// The order the tabs were remembered in, so that sessions arriving out
    /// of order still land where they sat.
    QStringList wanted;
    /// Which tab was in front when the application last closed. **Read once,
    /// at the start of the restore**: every session arriving writes the state
    /// file again, so by the time they are all in, the file no longer says
    /// what it said.
    QString wantedFront;
    /// A session asked for while the restore is going on, which takes the
    /// front from wantedFront once it has opened.
    QString askedFront;

    /// Whether every session a restore asked for has either arrived or been
    /// refused.
    [[nodiscard]] bool restoreSettled() const
    {
        return restoring && awaited.isEmpty();
    }

    [[nodiscard]] KConfigGroup group() const
    {
        const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
        return KConfigGroup(config, kGroup);
    }
};

Sessions::Sessions(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // **Attached here rather than only in create().** Whether QML reaches a
    // singleton through its create function or default-constructs it is
    // QML's business, and a singleton that only works one of those ways is
    // a singleton that works until it does not.
    attach(AppShell::engine());
}

Sessions::~Sessions() = default;

Sessions *Sessions::create(QQmlEngine *, QJSEngine *)
{
    return new Sessions;
}

void Sessions::setConfig(const KSharedConfig::Ptr &config)
{
    g_config = config;
}

void Sessions::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (!d->facade) {
        return;
    }

    connect(d->facade, &core::EngineFacade::sessionsListed, this, [this](const QString &json) {
        d->saved.clear();
        const QJsonArray listed = QJsonDocument::fromJson(json.toUtf8()).array();
        for (const QJsonValue &entry : listed) {
            const QJsonObject session = entry.toObject();
            const QString id = session.value(u"id"_s).toString();
            const QString name = session.value(u"name"_s).toString();
            if (id.isEmpty()) {
                continue;
            }
            d->saved.append({id, name});
            // A session that is open is called what the list says unless it
            // has been renamed since; the rename is what updates `names`.
            if (!d->names.contains(id)) {
                d->names.insert(id, name);
            }
        }
        d->listed = true;
        Q_EMIT savedChanged();
    });

    connect(d->facade, &core::EngineFacade::sessionOpened, this, [this](const QString &sessionId) {
        d->awaited.removeAll(sessionId);
        if (d->restoring && !d->askedFront.isEmpty() && sessionId == d->askedFront) {
            d->wantedFront = sessionId;
            d->askedFront.clear();
        }
        if (d->openIds.contains(sessionId)) {
            setCurrentId(sessionId); // asked for again: brought to the front
            return;
        }
        // **Put back where it sat.** A restore asks for its sessions in tab
        // order but they arrive in whatever order the engine reads them, so
        // the order a tab bar shows has to come from what was remembered
        // rather than from who answered first.
        d->openIds.insert(tabPositionFor(sessionId), sessionId);
        Q_EMIT openChanged();
        Q_EMIT opened(sessionId);
        // **The one just opened comes to the front**, unless a restore is
        // going on: then the tab that was in front does, once they are all
        // in, so that sessions arriving do not take it in turns.
        if (!d->restoring) {
            setCurrentId(sessionId);
        } else if (d->restoreSettled()) {
            finishRestore();
        }
        remember();
        // The list has one more in it than it did.
        d->facade->listSessions();
    });

    connect(d->facade, &core::EngineFacade::sessionClosed, this, [this](const QString &sessionId) {
        const qsizetype was = d->openIds.indexOf(sessionId);
        if (was < 0) {
            return;
        }
        d->openIds.removeAt(was);
        d->names.remove(sessionId);
        d->states.remove(sessionId);
        Q_EMIT openChanged();
        Q_EMIT closed(sessionId);
        if (d->currentId == sessionId) {
            // **The neighbour, not the first.** Closing a tab leaves the
            // one beside it in front, which is where the eye already is.
            const qsizetype next = std::min(was, d->openIds.size() - 1);
            setCurrentId(next < 0 ? QString{} : d->openIds.at(next));
        }
        remember();
    });

    /// Says what a session is called, from wherever the name came from.
    const auto learnName = [this](const QString &sessionId, const QString &name) {
        if (name.isEmpty() || d->names.value(sessionId) == name) {
            return;
        }
        d->names.insert(sessionId, name);
        if (d->openIds.contains(sessionId)) {
            Q_EMIT openChanged();
        }
        // The saved list carries names too, and a tab is not the only place
        // one is shown.
        Q_EMIT savedChanged();
    };

    // **Every open session's state, not just the one in front.** A tab
    // shows a live dot for its own session, and several sessions can run at
    // once — which is the whole point of having tabs.
    connect(d->facade, &core::EngineFacade::stateChanged, this, [this](const QString &sessionId, const QString &state) {
        if (d->states.value(sessionId) == state) {
            return;
        }
        d->states.insert(sessionId, state);
        if (d->openIds.contains(sessionId)) {
            Q_EMIT openChanged();
        }
    });

    // A session's name arrives with the whole session when it is opened.
    connect(d->facade,
            &core::EngineFacade::sessionSnapshot,
            this,
            [learnName](const QString &sessionId, quint64, const QString &sessionJson) {
                const QJsonObject session = QJsonDocument::fromJson(sessionJson.toUtf8()).object();
                learnName(sessionId, session.value(u"name"_s).toString());
            });

    // **And a rename arrives as a patch, not as another snapshot.** A rename
    // with no block id is the session's own name, which is the edit
    // command's convention; one with a block id is a block's nickname and
    // nothing to do with a tab.
    connect(d->facade,
            &core::EngineFacade::sessionPatch,
            this,
            [learnName](const QString &sessionId, quint64, const QString &patchJson) {
                const QJsonObject patch = QJsonDocument::fromJson(patchJson.toUtf8()).object();
                if (patch.value(u"op"_s).toString() != u"rename"_s || !patch.value(u"blockId"_s).toString().isEmpty()) {
                    return;
                }
                learnName(sessionId, patch.value(u"name"_s).toString());
            });

    // **A session that will not open is not an error to report twice.** The
    // engine has already said so — a file that was deleted behind the
    // application's back, or one it moved aside as corrupt — and what this
    // does is stop waiting for it, so that a restore whose last session is
    // missing still settles rather than leaving the window with no tab.
    connect(d->facade, &core::EngineFacade::error, this, [this](const QString &code, const QString &argsJson) {
        if (!d->restoring) {
            return;
        }
        const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
        if (code == "session.notFound"_L1) {
            d->awaited.removeAll(args.value(u"session"_s).toString());
        } else if (code == "session.corruptFile"_L1) {
            // The corrupt-file error names paths rather than the session, so
            // the id is recovered from the file name the engine gave.
            const QString path = args.value(u"path"_s).toString();
            for (const QString &waiting : std::as_const(d->awaited)) {
                if (path.contains(waiting)) {
                    d->awaited.removeAll(waiting);
                    break;
                }
            }
        } else {
            return;
        }
        if (d->restoreSettled()) {
            finishRestore();
        }
    });

    d->facade->listSessions();
}

QVariantList Sessions::saved() const
{
    QVariantList rows;
    rows.reserve(d->saved.size());
    for (const auto &[id, name] : d->saved) {
        rows.append(row(id, d->names.value(id, name), d->states.value(id)));
    }
    return rows;
}

QVariantList Sessions::open() const
{
    QVariantList rows;
    rows.reserve(d->openIds.size());
    for (const QString &id : d->openIds) {
        rows.append(row(id, d->names.value(id), d->states.value(id)));
    }
    return rows;
}

QString Sessions::currentId() const
{
    return d->currentId;
}

void Sessions::setCurrentId(const QString &sessionId)
{
    if (d->currentId == sessionId) {
        return;
    }
    d->currentId = sessionId;
    Q_EMIT currentChanged();
    remember();
}

bool Sessions::isReady() const
{
    return d->listed;
}

bool Sessions::isOpen(const QString &sessionId) const
{
    return d->openIds.contains(sessionId);
}

QString Sessions::nameOf(const QString &sessionId) const
{
    if (const auto found = d->names.constFind(sessionId); found != d->names.constEnd()) {
        return *found;
    }
    for (const auto &[id, name] : d->saved) {
        if (id == sessionId) {
            return name;
        }
    }
    return {};
}

void Sessions::restore(const QString &untitledName)
{
    if (!d->facade) {
        return;
    }
    // **Once per run, whoever asks.** A window calls this when it is built,
    // and a second window in the same process would otherwise ask for every
    // session again — or, mid-restore, start a second one over the first.
    if (!d->openIds.isEmpty() || d->restoring) {
        return;
    }
    const QStringList remembered = d->group().readEntry(kOpen, QStringList{});
    if (remembered.isEmpty()) {
        // A first run, or a state file somebody removed. The window is never
        // empty, so it gets one session.
        create(untitledName);
        return;
    }
    // Held while the sessions arrive, so that none of them takes the front
    // from whichever one had it, and so that a restore where every file has
    // gone still ends with a session to show.
    d->untitledName = untitledName;
    d->restoring = true;
    d->wanted = remembered;
    d->awaited = remembered;
    d->wantedFront = d->group().readEntry(kCurrent, QString{});
    for (const QString &sessionId : remembered) {
        // **Opened, never started.** A session that began making sound
        // because the application was opened would be a surprise nobody
        // asked for; the engine opens one stopped.
        d->facade->openSession(sessionId);
    }
}

qsizetype Sessions::tabPositionFor(const QString &sessionId) const
{
    const qsizetype wanted = d->wanted.indexOf(sessionId);
    if (wanted < 0) {
        return d->openIds.size(); // not part of a restore: it goes on the end
    }
    // The first open tab that was remembered after this one is where this one
    // belongs, so the bar ends up in the remembered order however the
    // sessions arrived.
    for (qsizetype at = 0; at < d->openIds.size(); ++at) {
        const qsizetype other = d->wanted.indexOf(d->openIds.at(at));
        if (other < 0 || other > wanted) {
            return at;
        }
    }
    return d->openIds.size();
}

void Sessions::finishRestore()
{
    d->restoring = false;
    d->wanted.clear();
    if (d->openIds.isEmpty()) {
        // Every remembered session has gone. The window is never empty.
        const QString name = d->untitledName;
        d->untitledName.clear();
        create(name);
        return;
    }
    d->untitledName.clear();
    // Whichever tab was in front comes back to the front, if it is still
    // there; otherwise the first one.
    const QString front = d->wantedFront;
    d->wantedFront.clear();
    d->askedFront.clear();
    setCurrentId(d->openIds.contains(front) ? front : d->openIds.constFirst());
    remember();
}

void Sessions::create(const QString &name)
{
    if (d->facade) {
        d->facade->createSession(name);
    }
}

void Sessions::openSession(const QString &sessionId)
{
    if (!d->facade) {
        return;
    }
    // **Asked for while the window is still putting its tabs back**, as
    // KRunner's Open does when it starts the window: this one is what was
    // asked for, so it is the one in front once the restore is done.
    if (d->restoring) {
        if (d->openIds.contains(sessionId)) {
            d->wantedFront = sessionId;
            return;
        }
        // **In front only once it has opened**: one that never does leaves
        // the front to the tab that had it.
        d->askedFront = sessionId;
        if (d->awaited.contains(sessionId)) {
            return; // on its way already, as part of the restore
        }
    }
    if (d->openIds.contains(sessionId)) {
        setCurrentId(sessionId);
        return;
    }
    d->facade->openSession(sessionId);
}

void Sessions::closeSession(const QString &sessionId)
{
    if (!d->facade || !d->openIds.contains(sessionId)) {
        return;
    }
    // **Never the last one.** A window with no session has nothing to show
    // and no way back to one.
    if (d->openIds.size() <= 1) {
        return;
    }
    d->facade->closeSession(sessionId);
}

void Sessions::renameSession(const QString &sessionId, const QString &name)
{
    if (!d->facade || name.isEmpty()) {
        return;
    }
    // **An empty blockId renames the session**, which is the edit command's
    // own convention. Renaming goes through an edit like everything else, so
    // it is undoable and autosaved like the rest — and so a tab can rename a
    // session that is not the one in front, which SessionModel cannot.
    const QJsonObject edit = core::editCommandToJson(core::Rename{QString{}, name});
    d->facade->applyEdit(sessionId, QString::fromUtf8(QJsonDocument(edit).toJson(QJsonDocument::Compact)));
}

void Sessions::deleteSession(const QString &sessionId)
{
    if (!d->facade) {
        return;
    }
    // Taken out of the remembered set before it goes, so that a launch after
    // this does not try to open a file that is not there.
    d->openIds.removeAll(sessionId);
    d->names.remove(sessionId);
    d->states.remove(sessionId);
    if (d->currentId == sessionId) {
        d->currentId = d->openIds.isEmpty() ? QString{} : d->openIds.constFirst();
        Q_EMIT currentChanged();
    }
    Q_EMIT openChanged();
    remember();
    d->facade->deleteSession(sessionId);
    d->facade->listSessions();
}

void Sessions::moveTab(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= d->openIds.size() || to >= d->openIds.size()) {
        return;
    }
    d->openIds.move(from, to);
    Q_EMIT openChanged();
    remember();
}

void Sessions::exportCurrent(const QUrl &file)
{
    if (!d->facade || d->currentId.isEmpty() || !file.isLocalFile()) {
        return;
    }
    d->facade->exportSession(d->currentId, file.toLocalFile());
}

void Sessions::importFrom(const QUrl &file)
{
    if (!d->facade || !file.isLocalFile()) {
        return;
    }
    // The imported session arrives as sessionOpened like any other, so it
    // gets a tab and comes to the front without this having to arrange it.
    d->facade->importSession(file.toLocalFile());
}

QString Sessions::fileSuffix()
{
    return QString(core::kSessionFileSuffix);
}

void Sessions::remember()
{
    // **Nothing is written down in the middle of a restore.** What it is
    // putting back is what the file says, and half a restore is not a state
    // worth remembering — least of all over the one being restored.
    if (d->restoring) {
        return;
    }
    KConfigGroup state = d->group();
    state.writeEntry(kOpen, d->openIds);
    state.writeEntry(kCurrent, d->currentId);
    // **Only the open ones.** A name kept for a session that is no longer
    // open is a name that outlives the thing it was for.
    state.deleteGroup(kNames);
    KConfigGroup names = state.group(kNames);
    for (const QString &id : std::as_const(d->openIds)) {
        names.writeEntry(id, d->names.value(id));
    }
    state.sync();
}

QString Sessions::rememberedName(const QString &sessionId)
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openStateConfig();
    return KConfigGroup(config, kGroup).group(kNames).readEntry(sessionId, QString{});
}

} // namespace rb::ui
