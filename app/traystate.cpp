// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "traystate.h"

#include "appshell.h"
#include "core/telemetry.h"
#include "dsp/decibel.h"
#include "notifications.h"

#include <KLocalizedString>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

constexpr auto kPeakRmsType = "meter.peakrms"_L1;
constexpr auto kVuType = "meter.vu"_L1;
constexpr auto kShowInTray = "showInTray"_L1;

[[nodiscard]] QJsonObject objectOf(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

/// Whether a block is a meter the person asked to see in the tray.
[[nodiscard]] bool isTrayMeter(const QString &type, const QJsonObject &params)
{
    return (type == kPeakRmsType || type == kVuType) && params.value(kShowInTray).toBool(false);
}

/// Whether the tray should ask for attention over \a event: something went
/// wrong that the person may not have seen. **Good news is not a reason**,
/// and a finished recording is the ordinary end of one.
[[nodiscard]] bool asksForAttention(notify::Event event)
{
    switch (event) {
    case notify::Event::DeviceLost:
    case notify::Event::CaptureLost:
    case notify::Event::PluginFailed:
    case notify::Event::LowDiskSpace:
    case notify::Event::FeedbackLoop:
        return true;
    case notify::Event::RecordingFinished:
    case notify::Event::CaptureRegained:
    case notify::Event::Count:
        return false;
    }
    return false;
}

} // namespace

class TrayState::Private
{
public:
    struct Block {
        QString id;
        QString type;
        QString nickname;
        QJsonObject params;
    };

    struct Session {
        QString name;
        QString state;
        /// Whether its snapshot has come: only then are its blocks known.
        bool known = false;
        QList<Block> blocks;
    };

    QPointer<core::EngineFacade> facade;
    QHash<QString, Session> sessions;
    /// The open sessions, in the order they were opened: the meters' order.
    QStringList openOrder;
    /// Recorders writing a file, as "session/block", and those of them
    /// paused.
    QSet<QString> writing;
    QSet<QString> paused;
    notify::NotificationPolicy policy;
    bool noticed = false;
    QString noticedText;
    bool available = false;
    bool watching = false;
    qulonglong frame = 0;
    QTimer ticker;
    QElapsedTimer sinceTick;
    /// Each tray meter's level, by "session/block".
    QHash<QString, float> levels;
    QHash<QString, bool> active;

    [[nodiscard]] static QString keyOf(const QString &sessionId, const QString &blockId)
    {
        return sessionId + u'/' + blockId;
    }

    void forgetRecorders(const QString &sessionId)
    {
        const QString prefix = sessionId + u'/';
        const auto drop = [&prefix](QSet<QString> &keys) {
            for (auto it = keys.begin(); it != keys.end();) {
                it = it->startsWith(prefix) ? keys.erase(it) : std::next(it);
            }
        };
        drop(writing);
        drop(paused);
    }
};

TrayState::TrayState(core::EngineFacade *facade, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->ticker.setInterval(kLevelTickMs);
    connect(&d->ticker, &QTimer::timeout, this, &TrayState::readLevels);
    connect(&d->policy, &notify::NotificationPolicy::notify, this, [this](const notify::Notification &notification) {
        if (!asksForAttention(notification.event)) {
            return;
        }
        d->noticed = true;
        d->noticedText = notification.title;
        Q_EMIT changed();
    });
    attach(facade);
}

TrayState::~TrayState() = default;

TrayState *TrayState::instance()
{
    static TrayState *state = new TrayState(AppShell::engine());
    return state;
}

TrayState *TrayState::create(QQmlEngine *, QJSEngine *)
{
    TrayState *state = instance();
    // **One for the window and the icon**, which is why QML must not own it.
    QJSEngine::setObjectOwnership(state, QJSEngine::CppOwnership);
    // A test that makes a window of its own brings an engine of its own.
    if (state->d->facade != AppShell::engine()) {
        state->attach(AppShell::engine());
    }
    return state;
}

void TrayState::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
        d->facade->disconnect(&d->policy);
    }
    d->facade = facade;
    d->sessions.clear();
    d->openOrder.clear();
    d->writing.clear();
    d->paused.clear();
    d->levels.clear();
    d->active.clear();
    d->noticed = false;
    d->noticedText.clear();
    if (facade == nullptr) {
        Q_EMIT changed();
        Q_EMIT metersChanged();
        return;
    }

    connect(facade, &core::EngineFacade::sessionsListed, this, [this](const QString &json) {
        bool metersMoved = false;
        for (const QJsonValue &value : QJsonDocument::fromJson(json.toUtf8()).array()) {
            const QJsonObject entry = value.toObject();
            const QString id = entry.value("id"_L1).toString();
            if (id.isEmpty()) {
                continue;
            }
            Private::Session &session = d->sessions[id];
            session.name = entry.value("name"_L1).toString();
            session.state = entry.value("state"_L1).toString();
            const bool open = !session.state.isEmpty() && session.state != core::sessionState::closed;
            if (open && !d->openOrder.contains(id)) {
                d->openOrder.append(id);
                metersMoved = true;
            }
            // **Open, but its blocks never seen**: the tray was attached
            // after it opened. Asked for once.
            if (open && !session.known && d->facade) {
                d->facade->resynchronise(id);
            }
        }
        Q_EMIT changed();
        if (metersMoved) {
            Q_EMIT metersChanged();
        }
    });
    connect(facade, &core::EngineFacade::sessionOpened, this, [this](const QString &sessionId) {
        Private::Session &session = d->sessions[sessionId];
        if (session.state.isEmpty() || session.state == core::sessionState::closed) {
            session.state = core::sessionState::idle;
        }
        if (!d->openOrder.contains(sessionId)) {
            d->openOrder.append(sessionId);
        }
        // Its blocks come with the snapshot every engine sends right after.
        Q_EMIT changed();
    });
    connect(facade, &core::EngineFacade::sessionClosed, this, [this](const QString &sessionId) {
        Private::Session &session = d->sessions[sessionId];
        session.state = core::sessionState::closed;
        session.known = false;
        session.blocks.clear();
        d->openOrder.removeAll(sessionId);
        d->forgetRecorders(sessionId);
        Q_EMIT changed();
        Q_EMIT metersChanged();
    });
    connect(facade, &core::EngineFacade::stateChanged, this, [this](const QString &sessionId, const QString &state) {
        Private::Session &session = d->sessions[sessionId];
        session.state = state;
        // **A session that stops has no Recorder writing**, whatever signal
        // about its last file has yet to arrive.
        if (state != core::sessionState::running && state != core::sessionState::starting) {
            d->forgetRecorders(sessionId);
        }
        Q_EMIT changed();
    });
    connect(facade,
            &core::EngineFacade::sessionSnapshot,
            this,
            [this](const QString &sessionId, quint64, const QString &json) {
                const QJsonObject snapshot = objectOf(json);
                Private::Session &session = d->sessions[sessionId];
                if (snapshot.contains("name"_L1)) {
                    session.name = snapshot.value("name"_L1).toString();
                }
                session.known = true;
                session.blocks.clear();
                for (const QJsonValue &value : snapshot.value("blocks"_L1).toArray()) {
                    const QJsonObject block = value.toObject();
                    session.blocks.append({block.value("id"_L1).toString(),
                                           block.value("type"_L1).toString(),
                                           block.value("nickname"_L1).toString(),
                                           block.value("params"_L1).toObject()});
                }
                if (!d->openOrder.contains(sessionId)) {
                    d->openOrder.append(sessionId);
                }
                Q_EMIT changed();
                Q_EMIT metersChanged();
            });
    connect(facade,
            &core::EngineFacade::sessionPatch,
            this,
            [this](const QString &sessionId, quint64, const QString &json) {
                auto found = d->sessions.find(sessionId);
                if (found == d->sessions.end() || !found->known) {
                    return;
                }
                Private::Session &session = *found;
                const QJsonObject patch = objectOf(json);
                const QString op = patch.value("op"_L1).toString();
                const QString blockId = patch.value("blockId"_L1).toString();
                const auto block = std::find_if(session.blocks.begin(),
                                                session.blocks.end(),
                                                [&blockId](const Private::Block &b) { return b.id == blockId; });
                bool metersMoved = false;
                if (op == "addBlock"_L1) {
                    const QJsonObject added = patch.value("block"_L1).toObject();
                    Private::Block made{added.value("id"_L1).toString(),
                                        added.value("type"_L1).toString(),
                                        added.value("nickname"_L1).toString(),
                                        added.value("params"_L1).toObject()};
                    const qsizetype index = patch.value("index"_L1).toInteger(-1);
                    if (index >= 0 && index <= session.blocks.size()) {
                        session.blocks.insert(index, made);
                    } else {
                        session.blocks.append(made);
                    }
                    metersMoved = isTrayMeter(made.type, made.params);
                } else if (op == "removeBlock"_L1 && block != session.blocks.end()) {
                    metersMoved = isTrayMeter(block->type, block->params);
                    session.blocks.erase(block);
                } else if (op == "setParam"_L1 && block != session.blocks.end()) {
                    const bool before = isTrayMeter(block->type, block->params);
                    block->params.insert(patch.value("key"_L1).toString(), patch.value("value"_L1));
                    metersMoved = before != isTrayMeter(block->type, block->params);
                } else if (op == "applyPreset"_L1 && block != session.blocks.end()) {
                    const bool before = isTrayMeter(block->type, block->params);
                    const QJsonObject params = patch.value("params"_L1).toObject();
                    for (auto it = params.begin(); it != params.end(); ++it) {
                        block->params.insert(it.key(), it.value());
                    }
                    for (const QJsonValue &key : patch.value("clear"_L1).toArray()) {
                        block->params.remove(key.toString());
                    }
                    metersMoved = before != isTrayMeter(block->type, block->params);
                } else if (op == "rename"_L1) {
                    if (blockId.isEmpty()) {
                        session.name = patch.value("name"_L1).toString();
                        metersMoved = true;
                        Q_EMIT changed();
                    } else if (block != session.blocks.end()) {
                        block->nickname = patch.value("name"_L1).toString();
                        metersMoved = isTrayMeter(block->type, block->params);
                    }
                }
                if (metersMoved) {
                    Q_EMIT metersChanged();
                }
            });
    connect(facade,
            &core::EngineFacade::recordingFileStarted,
            this,
            [this](const QString &sessionId, const QString &blockId, const QString &) {
                d->writing.insert(Private::keyOf(sessionId, blockId));
                Q_EMIT changed();
            });
    connect(facade,
            &core::EngineFacade::recordingFileFinished,
            this,
            [this](const QString &sessionId, const QString &blockId, const QString &, const QString &) {
                const QString key = Private::keyOf(sessionId, blockId);
                d->writing.remove(key);
                d->paused.remove(key);
                Q_EMIT changed();
            });
    connect(facade,
            &core::EngineFacade::recordingPaused,
            this,
            [this](const QString &sessionId, const QString &blockId, bool paused) {
                const QString key = Private::keyOf(sessionId, blockId);
                if (paused) {
                    d->paused.insert(key);
                } else {
                    d->paused.remove(key);
                }
                Q_EMIT changed();
            });
    connect(facade, &core::EngineFacade::error, &d->policy, &notify::NotificationPolicy::error);
    connect(facade, &core::EngineFacade::notice, &d->policy, &notify::NotificationPolicy::notice);

    Q_EMIT changed();
    Q_EMIT metersChanged();
    facade->listSessions();
}

int TrayState::runningCount() const
{
    return static_cast<int>(std::count_if(d->sessions.cbegin(), d->sessions.cend(), [](const Private::Session &s) {
        return s.state == core::sessionState::running;
    }));
}

bool TrayState::isRecording() const
{
    // **Paused is not recording**: nothing is being captured, and the red
    // dot says that something is.
    return std::any_of(
        d->writing.cbegin(), d->writing.cend(), [this](const QString &key) { return !d->paused.contains(key); });
}

bool TrayState::needsAttention() const
{
    return d->noticed || std::any_of(d->sessions.cbegin(), d->sessions.cend(), [](const Private::Session &s) {
               return s.state == core::sessionState::error;
           });
}

QString TrayState::attentionText() const
{
    if (d->noticed) {
        return d->noticedText;
    }
    if (needsAttention()) {
        return i18nc("@info:tooltip in the system tray", "A session stopped because of an error");
    }
    return {};
}

QString TrayState::status() const
{
    if (needsAttention()) {
        return kAttention;
    }
    return runningCount() > 0 || isRecording() ? QString(kActive) : QString(kPassive);
}

QString TrayState::toolTip() const
{
    if (needsAttention()) {
        return attentionText();
    }
    const int running = runningCount();
    if (running == 0) {
        return i18nc("@info:tooltip in the system tray", "No session running");
    }
    if (isRecording()) {
        return i18ncp("@info:tooltip in the system tray",
                      "%1 session running, recording",
                      "%1 sessions running, recording",
                      running);
    }
    return i18ncp("@info:tooltip in the system tray", "%1 session running", "%1 sessions running", running);
}

QVariantList TrayState::meters() const
{
    QVariantList list;
    for (const QString &sessionId : std::as_const(d->openOrder)) {
        const auto found = d->sessions.constFind(sessionId);
        if (found == d->sessions.cend()) {
            continue;
        }
        for (const Private::Block &block : found->blocks) {
            if (!isTrayMeter(block.type, block.params)) {
                continue;
            }
            list.append(QVariantMap{{u"sessionId"_s, sessionId},
                                    {u"sessionName"_s, found->name},
                                    {u"blockId"_s, block.id},
                                    {u"type"_s, block.type},
                                    {u"nickname"_s, block.nickname}});
        }
    }
    return list;
}

bool TrayState::isAvailable() const
{
    return d->available;
}

void TrayState::setAvailable(bool available)
{
    if (d->available == available) {
        return;
    }
    d->available = available;
    Q_EMIT availableChanged();
}

bool TrayState::isWatching() const
{
    return d->watching;
}

void TrayState::setWatching(bool watching)
{
    if (d->watching == watching) {
        return;
    }
    d->watching = watching;
    if (watching) {
        d->sinceTick.invalidate();
        readLevels();
        d->ticker.start();
    } else {
        d->ticker.stop();
    }
    Q_EMIT watchingChanged();
}

qulonglong TrayState::frame() const
{
    return d->frame;
}

void TrayState::acknowledge()
{
    if (!d->noticed) {
        return;
    }
    d->noticed = false;
    d->noticedText.clear();
    Q_EMIT changed();
}

QVariantMap TrayState::meterLevel(const QString &sessionId, const QString &blockId) const
{
    const QString key = Private::keyOf(sessionId, blockId);
    return QVariantMap{{u"peakDb"_s, static_cast<double>(d->levels.value(key, dsp::kSilenceDb))},
                       {u"active"_s, d->active.value(key, false)}};
}

void TrayState::requestPopover()
{
    Q_EMIT popoverRequested();
}

void TrayState::requestWindowToggle()
{
    Q_EMIT windowToggleRequested();
}

void TrayState::requestSessionsWindow()
{
    Q_EMIT sessionsWindowRequested();
}

void TrayState::bringForward(const QString &sessionId, const QString &blockId)
{
    Q_EMIT forwardRequested(sessionId, blockId);
}

void TrayState::readLevels()
{
    // **The fall is by the clock**, from the last read, so a slow tick does
    // not make a slow meter.
    qint64 elapsed = 0;
    if (d->sinceTick.isValid()) {
        elapsed = d->sinceTick.restart();
    } else {
        d->sinceTick.start();
    }
    const float fall = kFallDbPerSecond * static_cast<float>(elapsed) / 1000.0f;
    QSet<QString> seen;
    for (const QVariant &value : meters()) {
        const QVariantMap meter = value.toMap();
        const QString sessionId = meter.value(u"sessionId"_s).toString();
        const QString blockId = meter.value(u"blockId"_s).toString();
        const QString key = Private::keyOf(sessionId, blockId);
        seen.insert(key);
        float loudest = dsp::kSilenceDb;
        bool active = false;
        const auto session = d->sessions.constFind(sessionId);
        const bool running = session != d->sessions.cend() && session->state == core::sessionState::running;
        if (const core::TelemetrySlot *slot = running ? AppShell::telemetryOf(sessionId) : nullptr; slot != nullptr) {
            const core::TelemetrySnapshot snapshot = slot->load();
            const QByteArray id = blockId.toUtf8().left(core::kMaxTelemetryIdBytes - 1);
            const std::uint32_t blocks = std::min<std::uint32_t>(snapshot.blockCount, core::kMaxMeteredBlocks);
            for (std::uint32_t block = 0; block < blocks; ++block) {
                const core::BlockTelemetry &levels = snapshot.blocks[block];
                if (std::strncmp(levels.id, id.constData(), core::kMaxTelemetryIdBytes) != 0) {
                    continue;
                }
                const std::uint32_t channels = std::min<std::uint32_t>(levels.channelCount, rt::kMaxMeterChannels);
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    loudest = std::max(loudest, dsp::gainToDb(levels.peak[channel]));
                }
                active = levels.active;
                break;
            }
        }
        const float previous = d->levels.value(key, dsp::kSilenceDb);
        d->levels.insert(key,
                         running ? std::max(loudest, std::max(dsp::kSilenceDb, previous - fall)) : dsp::kSilenceDb);
        d->active.insert(key, active);
    }
    // A meter that is gone keeps no level for when it comes back.
    for (auto it = d->levels.begin(); it != d->levels.end();) {
        it = seen.contains(it.key()) ? std::next(it) : d->levels.erase(it);
    }
    ++d->frame;
    Q_EMIT ticked();
}

} // namespace rb::ui
