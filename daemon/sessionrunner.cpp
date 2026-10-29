// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sessionrunner.h"

#include "core/enginefacade.h"
#include "texts.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcRunner, "reverblabr.daemon.runner")

namespace rb::daemon
{

namespace
{

/// A property of a systemd unit, as StartTransientUnit takes it: `(sv)`.
struct UnitProperty {
    QString name;
    QDBusVariant value;
};

/// A unit started along with the one asked for: `(sa(sv))`. None is, but
/// the call's signature has a place for them.
struct AuxiliaryUnit {
    QString name;
    QList<UnitProperty> properties;
};

} // namespace

} // namespace rb::daemon

Q_DECLARE_METATYPE(rb::daemon::UnitProperty)
Q_DECLARE_METATYPE(rb::daemon::AuxiliaryUnit)

namespace rb::daemon
{

namespace
{

QDBusArgument &operator<<(QDBusArgument &argument, const UnitProperty &property)
{
    argument.beginStructure();
    argument << property.name << property.value;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, UnitProperty &property)
{
    argument.beginStructure();
    argument >> property.name >> property.value;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const AuxiliaryUnit &unit)
{
    argument.beginStructure();
    argument << unit.name << unit.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, AuxiliaryUnit &unit)
{
    argument.beginStructure();
    argument >> unit.name >> unit.properties;
    argument.endStructure();
    return argument;
}

/// The application's id, which its icon, its desktop entry and the scope a
/// launcher puts it in are all named after.
constexpr auto kApplicationId = "io.github.reverblabr.Reverblabr";

constexpr auto kOpenAction = "open";
/// The match for "rb rec" when nothing is being recorded; a session's
/// recording match is this, a slash and its id. A session's own match is
/// its id, which never has a slash in it.
constexpr auto kRecordingMatch = "recording";

/// KRunner's QueryMatch::CategoryRelevance, which places a match among
/// other runners' before its relevance places it among ours.
enum CategoryRelevance : int {
    Moderate = 50,
    High = 70,
    Highest = 100,
};

/// What a query for this runner starts with. The desktop file's
/// X-Plasma-Runner-Match-Regex says the same, so KRunner does not call for
/// any other query.
constexpr auto kKeyword = "rb";
/// The fewest letters of "recording" that ask what is being recorded:
/// fewer would take every session whose name starts with "re".
constexpr qsizetype kRecordingLetters = 3;
/// How long a list read for one keystroke serves the next ones, for what
/// no signal says (a session file deleted or copied in by hand).
constexpr qint64 kListingMilliseconds = 2000;

QString keyOf(const QString &sessionId, const QString &blockId)
{
    return sessionId + u'/' + blockId;
}

/// What follows the keyword in \a query, or nothing when \a query is not
/// for this runner. Empty for the keyword alone, which lists every session.
std::optional<QString> termOf(const QString &query)
{
    const QString trimmed = query.trimmed();
    const QLatin1StringView keyword(kKeyword);
    if (!trimmed.startsWith(keyword, Qt::CaseInsensitive)) {
        return std::nullopt;
    }
    if (trimmed.size() > keyword.size() && !trimmed.at(keyword.size()).isSpace()) {
        return std::nullopt; // "rbx" is somebody else's word
    }
    return trimmed.mid(keyword.size()).trimmed();
}

/// Whether \a term asks what is being recorded: the start of "recording",
/// three letters or more, in English or in the person's language.
bool asksForRecording(const QString &term)
{
    if (term.size() < kRecordingLetters) {
        return false;
    }
    // i18n: typed in KRunner after "rb" to see what is being recorded; three letters or more of it do
    const notify::Message keyword = RB_I18NC("@info:keyword KRunner", "recording");
    const QString word = notify::translate(keyword);
    return u"recording"_s.startsWith(term, Qt::CaseInsensitive) || word.startsWith(term, Qt::CaseInsensitive);
}

QString stateText(const QString &state, bool recording)
{
    if (state == core::sessionState::running) {
        return recording ? notify::translate(RB_I18NC("@info:status session state in KRunner", "Running and recording"))
                         : notify::translate(RB_I18NC("@info:status session state in KRunner", "Running"));
    }
    if (state == core::sessionState::starting) {
        return notify::translate(RB_I18NC("@info:status session state in KRunner", "Starting"));
    }
    if (state == core::sessionState::stopping) {
        return notify::translate(RB_I18NC("@info:status session state in KRunner", "Stopping"));
    }
    if (state == core::sessionState::error) {
        return notify::translate(RB_I18NC("@info:status session state in KRunner", "Could not run"));
    }
    // Idle, or only on disk: either way, stopped.
    return notify::translate(RB_I18NC("@info:status session state in KRunner", "Stopped"));
}

/// Whether the match itself stops the session rather than running it.
bool stops(const QString &state)
{
    return state == core::sessionState::running || state == core::sessionState::starting;
}

QJsonArray sessionsIn(const QString &listing)
{
    return QJsonDocument::fromJson(listing.toUtf8()).array();
}

} // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const RunnerAction &action)
{
    argument.beginStructure();
    argument << action.id << action.text << action.iconName;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, RunnerAction &action)
{
    argument.beginStructure();
    argument >> action.id >> action.text >> action.iconName;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const RunnerMatch &match)
{
    argument.beginStructure();
    argument << match.id << match.text << match.iconName << match.categoryRelevance << match.relevance
             << match.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, RunnerMatch &match)
{
    argument.beginStructure();
    argument >> match.id >> match.text >> match.iconName >> match.categoryRelevance >> match.relevance
        >> match.properties;
    argument.endStructure();
    return argument;
}

void registerRunnerTypes()
{
    qDBusRegisterMetaType<RunnerAction>();
    qDBusRegisterMetaType<RunnerActions>();
    qDBusRegisterMetaType<RunnerMatch>();
    qDBusRegisterMetaType<RunnerMatches>();
}

SessionRunner::SessionRunner(core::EngineFacade *engine,
                             std::function<QString()> listing,
                             QString windowProgram,
                             QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_listing(std::move(listing))
    , m_windowProgram(std::move(windowProgram))
{
    registerRunnerTypes();

    connect(m_engine, &core::EngineFacade::sessionOpened, this, &SessionRunner::opened);
    connect(m_engine, &core::EngineFacade::sessionClosed, this, [this](const QString &sessionId) {
        m_toStart.remove(sessionId);
        forgetRecorders(sessionId);
        forgetListing();
    });
    // **Somebody else's list is as good as ours**, and newer.
    connect(m_engine, &core::EngineFacade::sessionsListed, this, [this](const QString &sessionsJson) {
        m_listed = sessionsJson;
        m_listedAt.start();
    });
    connect(
        m_engine, &core::EngineFacade::sessionPatch, this, [this](const QString &, quint64, const QString &patchJson) {
            // A rename of the session (or of a block, which costs only a
            // read): the name is what a query looks for.
            if (QJsonDocument::fromJson(patchJson.toUtf8()).object().value("op"_L1).toString() == "rename"_L1) {
                forgetListing();
            }
        });
    connect(m_engine, &core::EngineFacade::stateChanged, this, [this](const QString &sessionId, const QString &state) {
        forgetListing();
        // **A session that stops has no recorder writing**, whatever signal
        // about its last file has yet to arrive.
        if (state != core::sessionState::running && state != core::sessionState::starting) {
            forgetRecorders(sessionId);
        }
    });
    connect(m_engine,
            &core::EngineFacade::recordingFileStarted,
            this,
            [this](const QString &sessionId, const QString &blockId, const QString &) {
                m_writing.insert(keyOf(sessionId, blockId));
            });
    connect(m_engine,
            &core::EngineFacade::recordingFileFinished,
            this,
            [this](const QString &sessionId, const QString &blockId, const QString &, const QString &) {
                const QString key = keyOf(sessionId, blockId);
                m_writing.remove(key);
                m_paused.remove(key);
            });
    connect(m_engine,
            &core::EngineFacade::recordingPaused,
            this,
            [this](const QString &sessionId, const QString &blockId, bool paused) {
                const QString key = keyOf(sessionId, blockId);
                if (paused) {
                    m_paused.insert(key);
                } else {
                    m_paused.remove(key);
                }
            });
}

bool SessionRunner::publish(QDBusConnection bus)
{
    return bus.registerObject(kRunnerPath, this, QDBusConnection::ExportScriptableSlots);
}

RunnerActions SessionRunner::Actions()
{
    return {
        {QLatin1StringView(kOpenAction),
         notify::translate(RB_I18NC("@action:button in KRunner", "Open in Reverblabr")),
         u"window-new"_s},
    };
}

RunnerMatches SessionRunner::Match(const QString &query)
{
    const std::optional<QString> term = termOf(query);
    if (!term) {
        return {};
    }
    const QJsonArray sessions = sessionsIn(listing());
    RunnerMatches matches;

    if (asksForRecording(*term)) {
        for (const QJsonValue &value : sessions) {
            const QJsonObject session = value.toObject();
            const QString id = session.value("id"_L1).toString();
            if (!hasRecorders(id)) {
                continue;
            }
            const QString name = session.value("name"_L1).toString();
            const QString text = isRecording(id)
                ? notify::translate(RB_I18NC("@info KRunner, %1 is a session's name", "Recording in “%1”")).arg(name)
                : notify::translate(RB_I18NC("@info KRunner, %1 is a session's name", "Recording paused in “%1”"))
                      .arg(name);
            matches.append({QLatin1StringView(kRecordingMatch) + u'/' + id,
                            text,
                            QLatin1StringView(kApplicationId),
                            CategoryRelevance::Highest,
                            1.0,
                            {{u"actions"_s, QStringList{}}}});
        }
        if (matches.isEmpty()) {
            matches.append({QLatin1StringView(kRecordingMatch),
                            notify::translate(RB_I18NC("@info KRunner", "Nothing is being recorded")),
                            QLatin1StringView(kApplicationId),
                            CategoryRelevance::Highest,
                            1.0,
                            {{u"actions"_s, QStringList{}}}});
        }
    }

    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        const QString id = session.value("id"_L1).toString();
        const QString name = session.value("name"_L1).toString();
        if (id.isEmpty() || !name.contains(*term, Qt::CaseInsensitive)) {
            continue;
        }
        // **The closer the name, the higher it stands**: the whole name, then
        // its start, then anywhere in it; the keyword alone lists them all.
        int category = CategoryRelevance::High;
        double relevance = 0.6;
        if (term->isEmpty()) {
            category = CategoryRelevance::Moderate;
            relevance = 0.5;
        } else if (name.compare(*term, Qt::CaseInsensitive) == 0) {
            category = CategoryRelevance::Highest;
            relevance = 1.0;
        } else if (name.startsWith(*term, Qt::CaseInsensitive)) {
            relevance = 0.8;
        }
        const QString state = session.value("state"_L1).toString();
        // **The match itself runs or stops the session**, as its global
        // shortcut does, and says which in its words; Open is the action
        // beside it.
        const QString text = stops(state)
            ? notify::translate(RB_I18NC("@action KRunner, %1 is a session's name", "Stop “%1”")).arg(name)
            : notify::translate(RB_I18NC("@action KRunner, %1 is a session's name", "Run “%1”")).arg(name);
        matches.append({id,
                        text,
                        QLatin1StringView(kApplicationId),
                        category,
                        relevance,
                        {
                            {u"subtext"_s, stateText(state, isRecording(id))},
                            {u"actions"_s, QStringList{QLatin1StringView(kOpenAction)}},
                        }});
    }
    return matches;
}

void SessionRunner::Run(const QString &matchId, const QString &actionId)
{
    // **A token is for this one launch**, whatever Run does with it.
    const QString token = std::exchange(m_activationToken, QString{});
    if (matchId == QLatin1StringView(kRecordingMatch)) {
        return; // only says that nothing is
    }
    const QString recordingPrefix = QLatin1StringView(kRecordingMatch) + u'/';
    if (matchId.startsWith(recordingPrefix)) {
        openInWindow(matchId.mid(recordingPrefix.size()), token);
        return;
    }
    if (actionId == QLatin1StringView(kOpenAction)) {
        openInWindow(matchId, token);
        return;
    }
    if (!actionId.isEmpty()) {
        qCWarning(lcRunner) << "no such action:" << actionId;
        return;
    }
    // **Where the session stands now**, not when the match was made: it may
    // have stopped since.
    forgetListing();
    for (const QJsonValue &value : sessionsIn(listing())) {
        const QJsonObject session = value.toObject();
        if (session.value("id"_L1).toString() != matchId) {
            continue;
        }
        const QString state = session.value("state"_L1).toString();
        if (stops(state)) {
            qCInfo(lcRunner) << "stopping" << session.value("name"_L1).toString();
            m_engine->stop(matchId);
        } else if (state == core::sessionState::closed) {
            // **Opened first when it is only on disk**, then run once it has
            // opened, and left open: a window started later gets its tab.
            m_toStart.insert(matchId);
            m_engine->openSession(matchId);
            // **The engine opens a session before the call returns**, so an id
            // still waiting here is one that did not open (its file has gone,
            // or will not load), and the engine has said why.
            m_toStart.remove(matchId);
        } else {
            qCInfo(lcRunner) << "running" << session.value("name"_L1).toString();
            m_engine->start(matchId);
        }
        return;
    }
    qCInfo(lcRunner) << "no session" << matchId << "to run or stop";
}

void SessionRunner::SetActivationToken(const QString &token)
{
    m_activationToken = token;
}

void SessionRunner::Teardown()
{
    // A token is for the launch it came with, not one a query later; and the
    // next query reads the sessions afresh.
    m_activationToken.clear();
    forgetListing();
}

QString SessionRunner::listing()
{
    if (!m_listed || !m_listedAt.isValid() || m_listedAt.elapsed() > kListingMilliseconds) {
        m_listed = m_listing();
        m_listedAt.start();
    }
    return *m_listed;
}

void SessionRunner::forgetListing()
{
    m_listed.reset();
}

void SessionRunner::opened(const QString &sessionId)
{
    forgetListing();
    if (m_toStart.remove(sessionId)) {
        qCInfo(lcRunner) << "running" << sessionId << "now that it is open";
        m_engine->start(sessionId);
    }
}

void SessionRunner::forgetRecorders(const QString &sessionId)
{
    const QString prefix = sessionId + u'/';
    const auto drop = [&prefix](QSet<QString> &keys) {
        for (auto it = keys.begin(); it != keys.end();) {
            it = it->startsWith(prefix) ? keys.erase(it) : std::next(it);
        }
    };
    drop(m_writing);
    drop(m_paused);
}

void SessionRunner::openInWindow(const QString &sessionId, const QString &token)
{
    // **Only a session's id reaches a command line**, written the way the
    // engine writes ids, and joined to its option, so nothing KRunner was
    // handed can be read by the window as an option of its own.
    const QUuid id(sessionId);
    if (id.isNull()) {
        qCWarning(lcRunner) << "not a session's id:" << sessionId;
        return;
    }
    const QString argument = u"--open-session="_s + id.toString(QUuid::WithoutBraces);
    if (!startWindow(QDBusConnection::sessionBus(), m_windowProgram, {argument}, token)) {
        qCWarning(lcRunner) << "could not start" << m_windowProgram;
    }
}

bool SessionRunner::isRecording(const QString &sessionId) const
{
    // **Paused is not recording**: nothing is being captured.
    const QString prefix = sessionId + u'/';
    return std::any_of(m_writing.cbegin(), m_writing.cend(), [this, &prefix](const QString &key) {
        return key.startsWith(prefix) && !m_paused.contains(key);
    });
}

bool SessionRunner::hasRecorders(const QString &sessionId) const
{
    const QString prefix = sessionId + u'/';
    return std::any_of(
        m_writing.cbegin(), m_writing.cend(), [&prefix](const QString &key) { return key.startsWith(prefix); });
}

bool startWindow(const QDBusConnection &bus, const QString &program, const QStringList &arguments, const QString &token)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // **What lets the window come forward on Wayland**, where a window may
    // not take the focus by itself; KDBusService in the window reads it, and
    // hands it on when another Reverblabr is running already.
    if (token.isEmpty()) {
        environment.remove(u"XDG_ACTIVATION_TOKEN"_s);
    } else {
        environment.insert(u"XDG_ACTIVATION_TOKEN"_s, token);
    }
    process.setProcessEnvironment(environment);
    process.setStandardInputFile(QProcess::nullDevice());
    qint64 pid = 0;
    if (!process.startDetached(&pid) || pid <= 0) {
        return false;
    }

    // **Into a scope of its own, as a launcher puts an application**: named
    // after the application by the freedesktop convention, in app.slice. With
    // no user manager on the bus the call fails and the window simply stays
    // where it was started.
    qDBusRegisterMetaType<UnitProperty>();
    qDBusRegisterMetaType<QList<UnitProperty>>();
    qDBusRegisterMetaType<AuxiliaryUnit>();
    qDBusRegisterMetaType<QList<AuxiliaryUnit>>();
    QDBusMessage call = QDBusMessage::createMethodCall(u"org.freedesktop.systemd1"_s,
                                                       u"/org/freedesktop/systemd1"_s,
                                                       u"org.freedesktop.systemd1.Manager"_s,
                                                       u"StartTransientUnit"_s);
    const QString unit = u"app-%1-%2.scope"_s.arg(QLatin1StringView(kApplicationId)).arg(pid);
    const QList<UnitProperty> properties{
        {u"Description"_s, QDBusVariant(u"Reverblabr"_s)},
        {u"Slice"_s, QDBusVariant(u"app.slice"_s)},
        {u"PIDs"_s, QDBusVariant(QVariant::fromValue(QList<uint>{static_cast<uint>(pid)}))},
    };
    call.setArguments({unit, u"fail"_s, QVariant::fromValue(properties), QVariant::fromValue(QList<AuxiliaryUnit>{})});
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [unit](QDBusPendingCallWatcher *done) {
        done->deleteLater();
        if (done->isError()) {
            // **Said where it shows**: the window then goes when the engine's
            // unit is stopped or restarted.
            qCWarning(lcRunner) << "the window stays in the engine's own group, and stops with it:"
                                << done->error().name() << done->error().message();
        } else {
            qCDebug(lcRunner) << "the window is in" << unit;
        }
    });
    return true;
}

} // namespace rb::daemon
