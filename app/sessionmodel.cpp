// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sessionmodel.h"

#include "appshell.h"
#include "core/audiogramfit.h"
#include "core/editcommand.h"
#include "core/session.h"
#include "core/sessionstore.h"
#include "core/timers.h"

#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// What an undo step is called in a menu, from the code the engine gives
/// it (`edit.addBlock` and so on). **The interface's words, keyed by the
/// engine's codes**, as the error messages are: the engine has no words.
/// A code this build does not know gets no name, and the entry says plain
/// "Undo" rather than something made up.
[[nodiscard]] QString stepName(const QString &code)
{
    static const std::map<QString, KLazyLocalizedString> names{
        {u"edit.addBlock"_s, kli18nc("@action:inmenu an undo step, 'Undo Add Block'", "Add Block")},
        {u"edit.removeBlock"_s, kli18nc("@action:inmenu an undo step", "Remove Block")},
        {u"edit.moveBlock"_s, kli18nc("@action:inmenu an undo step", "Move Block")},
        {u"edit.connect"_s, kli18nc("@action:inmenu an undo step", "Connect")},
        {u"edit.disconnect"_s, kli18nc("@action:inmenu an undo step", "Disconnect")},
        {u"edit.setParam"_s, kli18nc("@action:inmenu an undo step", "Change Setting")},
        {u"edit.rename"_s, kli18nc("@action:inmenu an undo step", "Rename")},
        {u"edit.setNotes"_s, kli18nc("@action:inmenu an undo step", "Edit Notes")},
        {u"edit.setEnabled"_s, kli18nc("@action:inmenu an undo step", "Switch Block On or Off")},
        {u"edit.setSessionSetting"_s, kli18nc("@action:inmenu an undo step", "Change Session Setting")},
        {u"edit.applyPreset"_s, kli18nc("@action:inmenu an undo step", "Apply Preset")},
        {u"edit.resetParams"_s,
         kli18nc("@action:inmenu an undo step: a block's settings put back to their defaults", "Reset Settings")},
        {u"edit.setTimer"_s, kli18nc("@action:inmenu an undo step", "Change Timer")},
        {u"edit.removeTimer"_s, kli18nc("@action:inmenu an undo step", "Delete Timer")},
    };
    const auto found = names.find(code);
    return found == names.end() ? QString{} : found->second.toString();
}

/// A value as QML handed it over. **A JavaScript array or object arrives
/// wrapped in a QJSValue**, which QJsonValue turns into null rather than
/// into a list — so a panel writing a list parameter would write nothing
/// and nothing would say so. Everything else passes through untouched.
[[nodiscard]] QVariant unwrapped(const QVariant &value)
{
    return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

/// One value on its way to the engine. `SetParam` takes JSON so that a
/// number, a string, a bool and a list all fit one signature; Qt will not
/// parse a bare scalar as a document, so a single value travels wrapped in
/// an array and the engine unwraps it.
[[nodiscard]] QString asParamJson(const QVariant &value)
{
    const QJsonArray wrapped{QJsonValue::fromVariant(value)};
    return QString::fromUtf8(QJsonDocument(wrapped).toJson(QJsonDocument::Compact));
}

} // namespace

class SessionModel::Private
{
public:
    QPointer<core::EngineFacade> facade;
    QString sessionId;
    core::Session session;
    quint64 revision = 0;
    bool haveSnapshot = false;
    int sent = 0;
    /// The ids of blocks sent to the engine that the session has not got
    /// yet. **Kept out of the next block's way**: the session's next free id
    /// is the same id until the engine answers, and the engine in
    /// reverblabrd answers over the bus, so two blocks added before it had
    /// were both given the same one and the second was refused.
    QSet<QString> awaitedIds;

    /// The lowest "b<n>" that neither the session nor a block on its way
    /// to it has, as Session::nextBlockId() picks one.
    QString nextFreeBlockId() const
    {
        for (qsizetype number = 1;; ++number) {
            QString candidate = QStringLiteral("b%1").arg(number);
            if (!session.hasBlock(candidate) && !awaitedIds.contains(candidate)) {
                return candidate;
            }
        }
    }

    /// A value a control is still dragging, by block and parameter. **The
    /// latest one, not a queue**: nobody wants the values a slider passed
    /// through, only where it is now.
    std::map<std::pair<QString, QString>, QVariant> pending;
    QTimer live;

    /// A drag still under way, by block and parameter: the button went down
    /// and has not come up. What it last sent, when, and when the hand last
    /// moved.
    struct Held {
        QVariant value;
        std::int64_t sentAt = 0;
        std::int64_t movedAt = 0;
    };
    std::map<std::pair<QString, QString>, Held> held;
    /// **Keeps a still hand one step to undo.** The engine's history knows a
    /// drag only by its edits arriving close together, and a hand resting
    /// on a slider to look at the curve sends nothing; past the gesture
    /// window the rest of the drag became a second step. So while a drag is
    /// held, its value is sent again before the window closes.
    QTimer hold;
    QElapsedTimer clock;

    /// What the engine last said undo and redo would do, for this session.
    bool canUndo = false;
    bool canRedo = false;
    QString undoCode;
    QString redoCode;

    /// What the engine last said of each plugin block's own window and
    /// presets, by block id.
    int pluginRevision = 0;
    QHash<QString, bool> windowOpen;
    QHash<QString, QString> windowReason;
    QHash<QString, QVariantList> pluginPresets;
    QHash<QString, QString> presetShown;

    void send(const QString &blockId, const QString &paramId, const QVariant &raw)
    {
        if (!facade || sessionId.isEmpty()) {
            return;
        }
        const QVariant value = unwrapped(raw);
        ++sent;
        facade->setParam(sessionId, blockId, paramId, asParamJson(value));
        if (const auto found = held.find({blockId, paramId}); found != held.end()) {
            found->second.value = value;
            found->second.sentAt = clock.elapsed();
        }
        // **The engine sends no patch for a knob**, on purpose: it would be
        // sixty messages a second telling the client what it just did. So
        // the value is written here as it goes, and an undo — which *does*
        // arrive as a patch — overwrites it.
        for (core::BlockInstance &block : session.blocks) {
            if (block.id == blockId) {
                block.params.insert(paramId, QJsonValue::fromVariant(value));
                break;
            }
        }
    }

    /// One edit on its way to the engine. What it changes comes back as a
    /// patch; nothing is written here and hoped for.
    void apply(const core::EditCommand &command)
    {
        if (!facade || sessionId.isEmpty()) {
            return;
        }
        facade->applyEdit(
            sessionId,
            QString::fromUtf8(QJsonDocument(core::editCommandToJson(command)).toJson(QJsonDocument::Compact)));
    }

    /// Several edits as one thing the person did. **One call, not a loop of
    /// them**: the engine applies a list whole or not at all and records it
    /// as one step, which is what makes a stereo link one Ctrl+Z and makes
    /// a half-drawn one impossible.
    void applyAll(const std::vector<core::EditCommand> &commands)
    {
        if (!facade || sessionId.isEmpty() || commands.empty()) {
            return;
        }
        QJsonArray array;
        for (const core::EditCommand &command : commands) {
            array.append(core::editCommandToJson(command));
        }
        facade->applyEdit(sessionId, QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    }

    /// Takes \a blockId out of \a trial and sends the same to the engine:
    /// the block, and in automatic mode the wires that close the gap it
    /// leaves. **One list, worked out before it is sent.** The engine
    /// answers when it answers — the one in reverblabrd over the bus — so
    /// asking this model's own copy afterwards whether the block had gone
    /// was asking too early, and the lane was left in two halves. Sent
    /// together, the block and the wires that replace it are also one step
    /// to take back.
    void removeFrom(core::Session &trial, const QString &blockId)
    {
        // **What fed it and what it fed, read before it goes.** Once the
        // removal has been applied the wires have gone with it, and there
        // is nothing left to say which blocks were on either side.
        const QList<core::Wire> touching = trial.wiresTouching(blockId);
        std::vector<core::EditCommand> commands{core::RemoveBlock{blockId}};
        if (!core::applyEdit(trial, commands.front()) || !trial.settings.autoConnect) {
            applyAll(commands);
            return;
        }
        // **Channel to channel, not everything to everything.** A block
        // passes what arrives at its input n out of its output n, so the
        // wire that replaces it joins the same pair — the left of what fed
        // it to the left of what it fed. Joining every input to every output
        // would turn the removal of a stereo block into a mono sum of both
        // channels, which is not what was there a moment before.
        for (const core::Wire &upstream : touching) {
            if (upstream.toBlock != blockId) {
                continue;
            }
            for (const core::Wire &downstream : touching) {
                if (downstream.fromBlock != blockId || downstream.fromPort != upstream.toPort) {
                    continue;
                }
                core::Wire wire = upstream;
                wire.toBlock = downstream.toBlock;
                wire.toPort = downstream.toPort;
                if (core::connectRefusal(trial, wire)) {
                    continue;
                }
                (void)core::applyEdit(trial, core::Connect{wire, -1});
                commands.push_back(core::Connect{wire, -1});
            }
        }
        applyAll(commands);
    }

    /// Sends again every held value that has not gone for a while, and
    /// lets go of one whose hand has not moved for kHoldLimitMs.
    void keepHeld()
    {
        const std::int64_t now = clock.elapsed();
        for (auto it = held.begin(); it != held.end();) {
            if (now - it->second.movedAt > kHoldLimitMs) {
                it = held.erase(it);
                continue;
            }
            // Half an interval since the last send, not a whole one: the
            // timer and the sends are not in step, and waiting for a whole
            // one could leave nearly two between them.
            if (!pending.contains(it->first) && now - it->second.sentAt >= kHoldIntervalMs / 2) {
                const auto key = it->first;
                const QVariant value = it->second.value;
                ++it;
                send(key.first, key.second, value);
                continue;
            }
            ++it;
        }
        if (held.empty()) {
            hold.stop();
        }
    }

    /// Ends every held drag: an undo, another session, the window put away.
    void letGo()
    {
        held.clear();
        hold.stop();
    }

    void flush()
    {
        const std::map<std::pair<QString, QString>, QVariant> due = std::move(pending);
        pending.clear();
        for (const auto &[key, value] : due) {
            send(key.first, key.second, value);
        }
    }
};

SessionModel::SessionModel(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // See BlockCatalogue: the window's engine is attached here, so that it
    // makes no difference how QML chose to build the singleton.
    attach(AppShell::engine());
    d->live.setInterval(kLiveIntervalMs);
    connect(&d->live, &QTimer::timeout, this, [this] {
        if (d->pending.empty()) {
            // Nothing moved in the last sixtieth of a second, so there is
            // nothing to send and nothing to wake up for.
            d->live.stop();
            return;
        }
        d->flush();
        // What was just sent is in the copy params() reads, so whatever
        // draws from it can be drawn again: once a tick, however many
        // mouse events the tick took in.
        Q_EMIT paramRevisionChanged();
    });
    d->clock.start();
    d->hold.setInterval(kHoldIntervalMs);
    connect(&d->hold, &QTimer::timeout, this, [this] { d->keepHeld(); });
    // **A drag the window can no longer finish is over.** A slider whose
    // window went to the background or was closed under the pointer may
    // never hear its button come up, and a drag kept open for ever would
    // hold the history with it.
    if (auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(application, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) {
                d->letGo();
            }
        });
        connect(application, &QGuiApplication::focusWindowChanged, this, [this](QWindow *window) {
            if (window == nullptr) {
                d->letGo();
            }
        });
    }
}

SessionModel::~SessionModel() = default;

void SessionModel::announce()
{
    // **The bindings first, then whoever listens for the news.** A control
    // kept by SessionBound is set again on changed(), and what it is set
    // to is often a binding on paramRevision; told in the other order, it
    // would be set to what the session said before.
    Q_EMIT paramRevisionChanged();
    Q_EMIT changed();
}

SessionModel *SessionModel::create(QQmlEngine *, QJSEngine *)
{
    return new SessionModel;
}

void SessionModel::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade == nullptr) {
        return;
    }

    connect(facade,
            &core::EngineFacade::sessionSnapshot,
            this,
            [this](const QString &id, quint64 revision, const QString &sessionJson) {
                if (id != d->sessionId) {
                    return;
                }
                const QJsonObject json = QJsonDocument::fromJson(sessionJson.toUtf8()).object();
                const std::expected<core::Session, core::SessionError> read = core::sessionFromJson(json);
                if (!read) {
                    return;
                }
                d->session = *read;
                d->revision = revision;
                d->haveSnapshot = true;
                d->awaitedIds.removeIf([this](const QString &blockId) { return d->session.hasBlock(blockId); });
                announce();
            });

    connect(facade,
            &core::EngineFacade::sessionPatch,
            this,
            [this, facade](const QString &id, quint64 revision, const QString &patchJson) {
                if (id != d->sessionId || !d->haveSnapshot) {
                    return;
                }
                // **A gap is something to notice.** Patches are numbered so
                // that a client which has missed one can say so rather than
                // going on with a session that is quietly wrong.
                if (revision != d->revision + 1) {
                    d->haveSnapshot = false;
                    facade->resynchronise(d->sessionId);
                    return;
                }
                const QJsonObject json = QJsonDocument::fromJson(patchJson.toUtf8()).object();
                const std::expected<core::EditCommand, core::EditJsonError> command = core::editCommandFromJson(json);
                if (!command) {
                    d->haveSnapshot = false;
                    facade->resynchronise(d->sessionId);
                    return;
                }
                // **The engine's own applyEdit**, so that our copy and its
                // copy cannot drift: there is one definition of what an
                // edit does.
                if (!core::applyEdit(d->session, *command)) {
                    d->haveSnapshot = false;
                    facade->resynchronise(d->sessionId);
                    return;
                }
                d->revision = revision;
                if (const auto *added = std::get_if<core::AddBlock>(&*command)) {
                    d->awaitedIds.remove(added->block.id);
                }
                announce();
            });

    connect(facade,
            &core::EngineFacade::pluginWindowChanged,
            this,
            [this](const QString &id, const QString &blockId, bool open, const QString &reason) {
                if (id != d->sessionId) {
                    return;
                }
                d->windowOpen.insert(blockId, open);
                d->windowReason.insert(blockId, reason);
                ++d->pluginRevision;
                Q_EMIT pluginChanged();
            });
    connect(facade,
            &core::EngineFacade::pluginPresetsListed,
            this,
            [this](const QString &id, const QString &blockId, const QString &presetsJson) {
                if (id != d->sessionId) {
                    return;
                }
                d->pluginPresets.insert(blockId, QJsonDocument::fromJson(presetsJson.toUtf8()).array().toVariantList());
                ++d->pluginRevision;
                Q_EMIT pluginChanged();
            });
    connect(facade,
            &core::EngineFacade::pluginPresetShown,
            this,
            [this](const QString &id, const QString &blockId, const QString &presetId) {
                if (id != d->sessionId) {
                    return;
                }
                d->presetShown.insert(blockId, presetId);
                ++d->pluginRevision;
                Q_EMIT pluginChanged();
            });

    connect(facade,
            &core::EngineFacade::undoStateChanged,
            this,
            [this](const QString &id, bool canUndo, const QString &undoCode, bool canRedo, const QString &redoCode) {
                if (id != d->sessionId) {
                    return;
                }
                d->canUndo = canUndo;
                d->undoCode = undoCode;
                d->canRedo = canRedo;
                d->redoCode = redoCode;
                Q_EMIT undoStateChanged();
            });
}

QString SessionModel::sessionId() const
{
    return d->sessionId;
}

void SessionModel::setSessionId(const QString &sessionId)
{
    if (d->sessionId == sessionId) {
        return;
    }
    d->live.stop();
    d->pending.clear();
    d->letGo();
    d->sessionId = sessionId;
    d->session = {};
    d->awaitedIds.clear();
    d->revision = 0;
    d->haveSnapshot = false;
    // **Nothing to undo until the engine says otherwise**: the last
    // session's answer is not this one's.
    d->canUndo = false;
    d->canRedo = false;
    d->undoCode.clear();
    d->redoCode.clear();
    d->windowOpen.clear();
    d->windowReason.clear();
    d->pluginPresets.clear();
    d->presetShown.clear();
    ++d->pluginRevision;
    if (d->facade && !sessionId.isEmpty()) {
        d->facade->resynchronise(sessionId);
        d->facade->getUndoState(sessionId);
    }
    Q_EMIT sessionChanged();
    announce();
    Q_EMIT undoStateChanged();
    Q_EMIT pluginChanged();
}

bool SessionModel::canUndo() const
{
    return d->canUndo;
}

bool SessionModel::canRedo() const
{
    return d->canRedo;
}

QString SessionModel::undoText() const
{
    const QString step = d->canUndo ? stepName(d->undoCode) : QString{};
    return step.isEmpty() ? i18nc("@action:inmenu", "Undo")
                          : i18nc("@action:inmenu %1 is an undo step, 'Move Block'", "Undo %1", step);
}

QString SessionModel::redoText() const
{
    const QString step = d->canRedo ? stepName(d->redoCode) : QString{};
    return step.isEmpty() ? i18nc("@action:inmenu", "Redo")
                          : i18nc("@action:inmenu %1 is an undo step, 'Move Block'", "Redo %1", step);
}

void SessionModel::restartPlugin(const QString &blockId)
{
    if (d->facade && !d->sessionId.isEmpty()) {
        d->facade->restartPlugin(d->sessionId, blockId);
    }
}

int SessionModel::pluginRevision() const
{
    return d->pluginRevision;
}

void SessionModel::setPluginWindow(const QString &blockId, bool open)
{
    if (d->facade && !d->sessionId.isEmpty()) {
        d->facade->setPluginWindow(d->sessionId, blockId, open);
    }
}

bool SessionModel::pluginWindowOpen(const QString &blockId) const
{
    return d->windowOpen.value(blockId, false);
}

QString SessionModel::pluginWindowReason(const QString &blockId) const
{
    return d->windowReason.value(blockId);
}

void SessionModel::listPluginPresets(const QString &blockId)
{
    if (d->facade && !d->sessionId.isEmpty()) {
        d->facade->listPluginPresets(d->sessionId, blockId);
    }
}

QVariantList SessionModel::pluginPresets(const QString &blockId) const
{
    return d->pluginPresets.value(blockId);
}

void SessionModel::loadPluginPreset(const QString &blockId, const QString &presetId)
{
    if (d->facade && !d->sessionId.isEmpty()) {
        d->facade->loadPluginPreset(d->sessionId, blockId, presetId);
    }
}

void SessionModel::savePluginPreset(const QString &blockId, const QString &name)
{
    if (d->facade && !d->sessionId.isEmpty() && !name.trimmed().isEmpty()) {
        d->facade->savePluginPreset(d->sessionId, blockId, name.trimmed());
    }
}

QString SessionModel::pluginPresetShown(const QString &blockId) const
{
    return d->presetShown.value(blockId);
}

void SessionModel::undo()
{
    if (!d->facade || d->sessionId.isEmpty()) {
        return;
    }
    // **A drag still in the queue is part of the gesture being undone.**
    // Left there, it would arrive after the undo and put back what the undo
    // took away.
    d->live.stop();
    d->flush();
    // Nor may a held drag be sent again after it, for the same reason.
    d->letGo();
    d->facade->undo(d->sessionId);
}

void SessionModel::redo()
{
    if (!d->facade || d->sessionId.isEmpty()) {
        return;
    }
    d->live.stop();
    d->flush();
    d->letGo();
    d->facade->redo(d->sessionId);
}

QString SessionModel::name() const
{
    return d->session.name;
}

QString SessionModel::notes() const
{
    return d->session.notes;
}

bool SessionModel::autoConnect() const
{
    return d->session.settings.autoConnect;
}

QString SessionModel::sampleRateSetting() const
{
    return d->session.settings.sampleRate;
}

QString SessionModel::latency() const
{
    return d->session.settings.latency;
}

QString SessionModel::autoRun() const
{
    return d->session.settings.autoRun;
}

QStringList SessionModel::blockIds() const
{
    QStringList ids;
    ids.reserve(d->session.blocks.size());
    for (const core::BlockInstance &block : d->session.blocks) {
        ids.append(block.id);
    }
    return ids;
}

int SessionModel::wireCount() const
{
    return static_cast<int>(d->session.wires.size());
}

qulonglong SessionModel::revision() const
{
    return d->revision;
}

QVariantMap SessionModel::block(const QString &blockId) const
{
    for (const core::BlockInstance &instance : d->session.blocks) {
        if (instance.id != blockId) {
            continue;
        }
        return QVariantMap{
            {"id"_L1, instance.id},
            {"type"_L1, instance.type},
            {"nickname"_L1, instance.nickname},
            {"enabled"_L1, instance.enabled},
            {"position"_L1, instance.position},
            {"preset"_L1, instance.preset},
            {"unknownType"_L1, instance.unknownType},
        };
    }
    return {};
}

QString SessionModel::blockType(const QString &blockId) const
{
    const core::BlockInstance *instance = d->session.findBlock(blockId);
    return instance == nullptr ? QString{} : instance->type;
}

QPointF SessionModel::blockPosition(const QString &blockId) const
{
    const core::BlockInstance *instance = d->session.findBlock(blockId);
    return instance == nullptr ? QPointF{} : instance->position;
}

QVariantMap SessionModel::params(const QString &blockId) const
{
    for (const core::BlockInstance &instance : d->session.blocks) {
        if (instance.id == blockId) {
            return instance.params.toVariantMap();
        }
    }
    return {};
}

QVariant SessionModel::param(const QString &blockId, const QString &paramId) const
{
    // What a drag has not sent yet is still what the control is showing, so
    // a panel that reads back what it just set gets its own value rather
    // than the one before it.
    if (const auto found = d->pending.find({blockId, paramId}); found != d->pending.end()) {
        return found->second;
    }
    const QVariantMap all = params(blockId);
    return all.contains(paramId) ? all.value(paramId) : QVariant{};
}

void SessionModel::setParam(const QString &blockId, const QString &paramId, const QVariant &value, bool live)
{
    if (!live) {
        d->pending.erase({blockId, paramId});
        d->held.erase({blockId, paramId});
        d->send(blockId, paramId, value);
        // **A settled value is news; a knob being turned is not.** The
        // engine sends no patch for either, so if nothing said so here,
        // a device chosen in a block's own panel would leave that block's
        // face still naming the device before it. Once per gesture is
        // nothing like the sixty a second a drag would cost, which is why
        // the live path below only says paramRevisionChanged() as it goes.
        announce();
        return;
    }
    d->pending.insert_or_assign({blockId, paramId}, value);
    Private::Held &holding = d->held[{blockId, paramId}];
    holding.value = value;
    holding.movedAt = d->clock.elapsed();
    if (!d->hold.isActive()) {
        d->hold.start();
    }
    if (!d->live.isActive()) {
        // **The first value of a drag goes at once**, so that a control
        // never feels a sixtieth of a second behind the pointer; the rest
        // are coalesced until the timer comes round.
        d->flush();
        d->live.start();
        Q_EMIT paramRevisionChanged();
    }
}

void SessionModel::commitParam(const QString &blockId, const QString &paramId)
{
    d->held.erase({blockId, paramId});
    const auto found = d->pending.find({blockId, paramId});
    if (found != d->pending.end()) {
        const QVariant value = found->second;
        d->pending.erase(found);
        d->send(blockId, paramId, value);
    }
    // The end of a gesture, which is the moment the rest of the window is
    // allowed to hear about it. **Said even when nothing was left to send**:
    // a click on a track is one value, and the first value of a
    // gesture goes at once, so the pending list is empty by the time the
    // button comes up. Staying quiet then left every control reading the
    // value from before the gesture, and the handle jumped back to it.
    announce();
}

void SessionModel::setParams(const QString &blockId, const QVariantList &changes)
{
    std::vector<core::EditCommand> commands;
    for (const QVariant &entry : changes) {
        const QVariantList pair = entry.toList();
        if (pair.size() != 2) {
            continue;
        }
        const QString paramId = pair.at(0).toString();
        // A drag still pending on one of these would land after the edit
        // and undo it; the edit is the newer word.
        d->pending.erase({blockId, paramId});
        d->held.erase({blockId, paramId});
        commands.push_back(core::SetParam{blockId, paramId, QJsonValue::fromVariant(unwrapped(pair.at(1)))});
    }
    d->applyAll(commands);
}

void SessionModel::resetParams(const QString &blockId, const QStringList &paramIds)
{
    const core::BlockInstance *instance = d->session.findBlock(blockId);
    if (instance == nullptr) {
        return;
    }
    // **Only what is set is taken off.** A parameter the session does not
    // hold is at its default already, and a reset of nothing would still be
    // a step for undo to walk through.
    core::ApplyPreset reset;
    reset.blockId = blockId;
    for (const QString &paramId : paramIds) {
        if (instance->params.contains(paramId)) {
            reset.clear.append(paramId);
        }
        // A drag still pending would land after the reset and undo it.
        d->pending.erase({blockId, paramId});
    }
    if (reset.clear.isEmpty()) {
        return;
    }
    d->apply(reset);
}

QVariantList SessionModel::fitFromAudiogram(const QString &blockId)
{
    const core::BlockInstance *instance = d->session.findBlock(blockId);
    if (instance == nullptr) {
        return {};
    }
    const std::optional<core::AudiogramFit> fit = core::fitHearingBlock(*instance);
    if (!fit) {
        return {};
    }
    for (const core::EditCommand &command : fit->commands) {
        if (const auto *set = std::get_if<core::SetParam>(&command)) {
            // A drag still pending on a band would land after the fit.
            d->pending.erase({blockId, set->key});
            d->held.erase({blockId, set->key});
        }
    }
    d->applyAll(fit->commands);
    QVariantList bands;
    for (std::size_t band = 0; band < fit->bands.size(); ++band) {
        bands.append(QVariantMap{
            {u"band"_s, static_cast<int>(band)},
            // Where it is: a band's centre, or a point's place.
            {u"hz"_s, band < fit->centresHz.size() ? fit->centresHz[band] : 0.0},
            {u"noResponse"_s, fit->bands[band].noResponse},
            {u"beyondMaximum"_s, fit->bands[band].beyondMaximum},
        });
    }
    return bands;
}

void SessionModel::renameSession(const QString &name)
{
    if (name.isEmpty() || name == d->session.name) {
        return;
    }
    // An empty blockId renames the session, which is the edit command's own
    // convention and not an accident of this call.
    d->apply(core::Rename{QString{}, name});
}

void SessionModel::setNotes(const QString &notes)
{
    if (notes == d->session.notes) {
        return;
    }
    // **No emptiness check here**, deliberately: renameSession() above
    // refuses an empty name because a session nobody can name is a session
    // nobody finds again, while an empty note is somebody clearing the box.
    d->apply(core::SetNotes{notes});
}

void SessionModel::setAutoConnect(bool autoConnect)
{
    d->apply(core::SetSessionSetting{core::SessionSettingKey::AutoConnect, QJsonValue{autoConnect}});
}

void SessionModel::setSampleRateSetting(const QString &sampleRate)
{
    d->apply(core::SetSessionSetting{core::SessionSettingKey::SampleRate, QJsonValue{sampleRate}});
}

void SessionModel::setLatency(const QString &latency)
{
    d->apply(core::SetSessionSetting{core::SessionSettingKey::Latency, QJsonValue{latency}});
}

void SessionModel::setAutoRun(const QString &autoRun)
{
    d->apply(core::SetSessionSetting{core::SessionSettingKey::AutoRun, QJsonValue{autoRun}});
}

QVariantList SessionModel::timers() const
{
    QVariantList listed;
    const QDateTime now = QDateTime::currentDateTime();
    for (const core::Timer &timer : core::timersFromJson(d->session.timers)) {
        QVariantMap entry = core::timerToJson(timer).toVariantMap();
        if (const std::optional<QDateTime> next = core::nextRun(timer, now)) {
            entry.insert(u"nextRun"_s, *next);
        }
        listed.append(entry);
    }
    return listed;
}

namespace
{

/// A timer as QML handed it over, as the file holds it. Its days arrive as
/// a JavaScript array, which has to be unwrapped like any other.
[[nodiscard]] QJsonObject timerJson(const QVariantMap &timer)
{
    QJsonObject json;
    for (auto field = timer.cbegin(); field != timer.cend(); ++field) {
        if (field.key() != "nextRun"_L1) {
            json.insert(field.key(), QJsonValue::fromVariant(unwrapped(field.value())));
        }
    }
    return json;
}

} // namespace

QVariantMap SessionModel::newTimer() const
{
    core::Timer timer;
    timer.id = core::newTimerId();
    timer.repeat = core::TimerRepeat::Daily;
    timer.time = QTime(QTime::currentTime().addSecs(3600).hour(), 0);
    timer.durationSeconds = 3600;
    return core::timerToJson(timer).toVariantMap();
}

bool SessionModel::timerIsValid(const QVariantMap &timer) const
{
    // **A one-off in the past would never run**: not one to save, though a
    // session may hold one that has been and gone.
    const std::optional<core::Timer> read = core::timerFromJson(timerJson(timer));
    return read && (read->repeat != core::TimerRepeat::Once || core::nextRun(*read, QDateTime::currentDateTime()));
}

QString SessionModel::timerCalendar(const QVariantMap &timer) const
{
    const std::optional<core::Timer> read = core::timerFromJson(timerJson(timer));
    return read ? core::onCalendar(*read) : QString();
}

void SessionModel::setTimer(const QVariantMap &timer)
{
    d->apply(core::SetTimer{timerJson(timer), -1});
}

void SessionModel::setTimerEnabled(const QString &timerId, bool enabled)
{
    for (const core::Timer &timer : core::timersFromJson(d->session.timers)) {
        if (timer.id == timerId && timer.enabled != enabled) {
            core::Timer changed = timer;
            changed.enabled = enabled;
            d->apply(core::SetTimer{core::timerToJson(changed), -1});
        }
    }
}

void SessionModel::removeTimer(const QString &timerId)
{
    d->apply(core::RemoveTimer{timerId});
}

void SessionModel::renameBlock(const QString &blockId, const QString &nickname)
{
    core::Rename command;
    command.blockId = blockId;
    command.name = nickname;
    d->apply(command);
}

void SessionModel::setBlockEnabled(const QString &blockId, bool enabled)
{
    core::SetEnabled command;
    command.blockId = blockId;
    command.enabled = enabled;
    d->apply(command);
}

QString SessionModel::addBlock(const QString &typeId, const QPointF &position)
{
    if (typeId.isEmpty() || d->sessionId.isEmpty()) {
        return {};
    }
    core::BlockInstance block;
    // **The session's own next id**, worked out from the copy this model
    // already follows rather than from a counter of its own: a counter
    // would keep counting past an undo and leave gaps nobody can explain.
    // Less the ids of blocks still on their way, which that copy has not
    // got yet.
    block.id = d->nextFreeBlockId();
    block.type = typeId;
    // **A type with settings after it**: a library entry for one plugin is
    // a type and the plugin it is set to, `plugin.lv2?plugin=<id>`, and a
    // hearing block can start on the right ear, `?ear=right`
    // (SessionCanvas.withStartingEar()). One block, set, in one edit and
    // one step to undo.
    if (const qsizetype query = typeId.indexOf(u'?'); query >= 0) {
        block.type = typeId.left(query);
        const QUrlQuery settings(typeId.mid(query + 1));
        for (const auto &[key, value] : settings.queryItems(QUrl::FullyDecoded)) {
            block.params.insert(key, value);
        }
    }
    block.position = position;
    // The end of the list, which is the order the file holds and the order
    // the canvas tabs through. Where the tile *sits* is the position; where
    // it comes in the list is when it was added.
    d->awaitedIds.insert(block.id);
    d->apply(core::AddBlock{block, -1});
    return block.id;
}

void SessionModel::removeBlock(const QString &blockId)
{
    core::Session trial = d->session;
    d->removeFrom(trial, blockId);
}

void SessionModel::removeBlocks(const QStringList &blockIds)
{
    // **Each one the same as one on its own**, closing the gap behind it if
    // the session is set to. Taking three blocks out of a lane one at a time
    // leaves the lane joined up, because each removal wires what is left of
    // either side together. **Worked out on one copy**, which has each
    // removal in it before the next is worked out: the session this model
    // follows has none of them until the engine answers.
    core::Session trial = d->session;
    for (const QString &blockId : blockIds) {
        d->removeFrom(trial, blockId);
    }
}

void SessionModel::moveBlock(const QString &blockId, const QPointF &position)
{
    core::MoveBlock command;
    command.blockId = blockId;
    command.position = position;
    d->apply(command);
}

// --- wires ---

namespace
{

/// One wire as QML reads it. **The four ends and nothing else**: a wire has
/// no id anywhere in this application, because a wire is its two ends.
[[nodiscard]] QVariantMap wireMap(const core::Wire &wire)
{
    return QVariantMap{
        {"fromBlock"_L1, wire.fromBlock},
        {"fromPort"_L1, static_cast<int>(wire.fromPort)},
        {"toBlock"_L1, wire.toBlock},
        {"toPort"_L1, static_cast<int>(wire.toPort)},
    };
}

/// A wire from what QML was given. A negative port is nobody's port, and
/// clamping it to zero would turn a mistake into a wire somewhere else.
[[nodiscard]] core::Wire asWire(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort)
{
    core::Wire wire;
    wire.fromBlock = fromBlock;
    wire.fromPort = fromPort < 0 ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint32_t>(fromPort);
    wire.toBlock = toBlock;
    wire.toPort = toPort < 0 ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint32_t>(toPort);
    return wire;
}

} // namespace

QVariantList SessionModel::wires() const
{
    QVariantList list;
    list.reserve(d->session.wires.size());
    for (const core::Wire &wire : d->session.wires) {
        list.append(wireMap(wire));
    }
    return list;
}

QVariantList SessionModel::wiresTouching(const QString &blockId) const
{
    QVariantList list;
    for (const core::Wire &wire : d->session.wiresTouching(blockId)) {
        list.append(wireMap(wire));
    }
    return list;
}

int SessionModel::arrivingCount(const QString &blockId, int port) const
{
    int arriving = 0;
    for (const core::Wire &wire : d->session.wires) {
        if (wire.toBlock == blockId && wire.toPort == static_cast<std::uint32_t>(port)) {
            ++arriving;
        }
    }
    return arriving;
}

int SessionModel::portsInUse(const QString &blockId, bool output) const
{
    int needed = 0;
    for (const core::Wire &wire : d->session.wires) {
        if (output && wire.fromBlock == blockId) {
            needed = std::max(needed, static_cast<int>(wire.fromPort) + 1);
        }
        if (!output && wire.toBlock == blockId) {
            needed = std::max(needed, static_cast<int>(wire.toPort) + 1);
        }
    }
    return needed;
}

bool SessionModel::hasWire(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort) const
{
    return d->session.hasWire(asWire(fromBlock, fromPort, toBlock, toPort));
}

QString SessionModel::wireRefusal(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort) const
{
    const std::optional<core::EditError> refused =
        core::connectRefusal(d->session, asWire(fromBlock, fromPort, toBlock, toPort));
    if (!refused) {
        return {};
    }
    const std::string_view name = core::editErrorName(*refused);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

bool SessionModel::connectBlocks(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort)
{
    const core::Wire wire = asWire(fromBlock, fromPort, toBlock, toPort);
    // **Asked before it is sent, not after.** The caller has to know at once
    // whether the wire is there — a Ctrl-drag that was refused has to snap
    // back while the pointer is still where it was let go — and the engine
    // answers with a patch or with nothing, never with a return value.
    if (core::connectRefusal(d->session, wire)) {
        return false;
    }
    d->apply(core::Connect{wire, -1});
    return true;
}

void SessionModel::disconnectBlocks(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort)
{
    d->apply(core::Disconnect{asWire(fromBlock, fromPort, toBlock, toPort)});
}

namespace
{

/// Whether \a wire arrives at the lane that starts at \a firstPort and is
/// \a channels ports wide. Below nought is every lane.
[[nodiscard]] bool arrivesInLane(const core::Wire &wire, int firstPort, int channels)
{
    if (firstPort < 0) {
        return true;
    }
    const auto port = static_cast<qint64>(wire.toPort);
    return port >= firstPort && port < static_cast<qint64>(firstPort) + std::max(channels, 1);
}

} // namespace

int SessionModel::arrivingLinks(const QString &blockId, int firstPort, int channels) const
{
    QSet<QString> sources;
    for (const core::Wire &wire : d->session.wires) {
        if (wire.toBlock == blockId && arrivesInLane(wire, firstPort, channels)) {
            sources.insert(wire.fromBlock);
        }
    }
    return static_cast<int>(sources.size());
}

bool SessionModel::connectLanes(const QString &fromBlock, const QString &toBlock, int channels, int toFirstPort)
{
    // **Asked of a copy, because the answer depends on the ones before it.**
    // A second wire is refused for a cycle the first one made, so the
    // refusals have to be checked against the session as each wire leaves it
    // rather than against the session as it stands now.
    core::Session trial = d->session;
    std::vector<core::EditCommand> commands;
    for (int channel = 0; channel < channels; ++channel) {
        const core::Wire wire = asWire(fromBlock, channel, toBlock, std::max(toFirstPort, 0) + channel);
        if (core::connectRefusal(trial, wire)) {
            continue;
        }
        (void)core::applyEdit(trial, core::Connect{wire, -1});
        commands.push_back(core::Connect{wire, -1});
    }
    if (commands.empty()) {
        return false;
    }
    d->applyAll(commands);
    return true;
}

void SessionModel::disconnectLanes(const QString &fromBlock, const QString &toBlock, int toFirstPort, int channels)
{
    std::vector<core::EditCommand> commands;
    for (const core::Wire &wire : d->session.wires) {
        if (wire.fromBlock == fromBlock && wire.toBlock == toBlock && arrivesInLane(wire, toFirstPort, channels)) {
            commands.push_back(core::Disconnect{wire});
        }
    }
    d->applyAll(commands);
}

bool SessionModel::moveLane(const QString &fromBlock,
                            const QString &wasToBlock,
                            const QString &toBlock,
                            int channels,
                            int wasFirstPort,
                            int wasChannels,
                            int toFirstPort)
{
    // **Onto the lane it is already on is not a move.** Taking the link off
    // first would make drawing it again look allowed, and the session would
    // be told about an edit that changed nothing. Onto the other lane of the
    // same block is one.
    if (wasToBlock == toBlock && (wasFirstPort < 0 || wasFirstPort == std::max(toFirstPort, 0))) {
        return false;
    }
    core::Session trial = d->session;
    std::vector<core::EditCommand> commands;
    for (const core::Wire &wire : d->session.wires) {
        if (wire.fromBlock == fromBlock && wire.toBlock == wasToBlock
            && arrivesInLane(wire, wasFirstPort, wasChannels)) {
            (void)core::applyEdit(trial, core::Disconnect{wire});
            commands.push_back(core::Disconnect{wire});
        }
    }
    if (commands.empty()) {
        return false;
    }
    std::size_t drawn = 0;
    for (int channel = 0; channel < channels; ++channel) {
        const core::Wire wire = asWire(fromBlock, channel, toBlock, std::max(toFirstPort, 0) + channel);
        if (core::connectRefusal(trial, wire)) {
            continue;
        }
        (void)core::applyEdit(trial, core::Connect{wire, -1});
        commands.push_back(core::Connect{wire, -1});
        ++drawn;
    }
    if (drawn == 0) {
        // **Put back what was taken off.** A link dragged onto a lane that
        // will not have it has to end up where it started; leaving the
        // session with the link gone is the one outcome nobody asked for.
        return false;
    }
    d->applyAll(commands);
    return true;
}

int SessionModel::sentParamCount() const
{
    return d->sent;
}

} // namespace rb::ui
