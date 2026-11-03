// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "meterhub.h"
#include "rt/scope.h"
#include "scopeview.h"
#include "spectrumview.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace rb::ui
{

class ScopeHub;

/// **One block's sound as the window has it**, shared by every surface
/// showing that block: the tile and the card each draw it at their own width
/// from one look at the slot, taken at the block's own refresh rate.
///
/// It says what can honestly be drawn, too. A trace left standing looks just
/// like a steady sound, so a feed that can't vouch for it says why (State),
/// and a surface draws nothing rather than the last thing it saw. The one
/// trace that may stand still is one somebody asked to hold.
class ScopeFeed : public QObject
{
    Q_OBJECT

public:
    enum class State : std::uint8_t {
        Live, ///< New sound keeps arriving.
        Waiting, ///< Asked for, and not there yet.
        NoRoom, ///< Every slot of the session is in use.
        Stopped, ///< The session is not running, or has no such block.
        Off, ///< The block is switched off.
        Overloaded, ///< The session has given way and runs no blocks.
        NoNews, ///< Nobody is publishing, or nothing passes through the block.
    };

    /// How long a block may have no slot before it is said to have none.
    static constexpr qint64 kWaitForSlotMs = 1000;
    /// How long a slot may stand still before the sound is not believed:
    /// this, or two cycles, whichever is longer.
    static constexpr qint64 kUnmovedMs = 500;

    ScopeFeed(ScopeHub *hub, const QString &blockId);
    ~ScopeFeed() override;

    [[nodiscard]] const QString &blockId() const noexcept
    {
        return m_blockId;
    }

    /// What the block is set to, and whether to keep the trace it has and
    /// take no new ones, as \a surface says. **Every surface of one block
    /// says the same thing**, so whoever said it last is right, as long as it
    /// is still there: one going away may have said something else on its
    /// way, as a card being closed does, which is told it has no block.
    /// Held, nothing is written for the block, as far as this window goes.
    void tell(const void *surface, const ScopeSettings &settings, bool hold);
    /// \a surface has gone: what it said no longer counts.
    void forgetSurface(const void *surface);
    /// The same, said by nobody in particular.
    void setSettings(const ScopeSettings &settings);
    void setHold(bool hold);

    [[nodiscard]] State state() const noexcept
    {
        return m_state;
    }
    [[nodiscard]] bool isHeld() const noexcept
    {
        return m_hold;
    }
    /// Whether there is a trace that may be drawn now.
    [[nodiscard]] bool hasTrace() const noexcept
    {
        return !m_frame.points.empty() && (m_hold || m_state == State::Live);
    }

    [[nodiscard]] const ScopeFrame &frame() const noexcept
    {
        return m_frame;
    }

    /// The spectrum of what the frame holds, shared like the frame, so that
    /// a tile and a card showing one block cost one transform.
    [[nodiscard]] SpectrumAnalysis &spectrum() noexcept
    {
        return m_spectrum;
    }

    /// How much new sound a second can bring at this cycle length: the most
    /// a refresh rate above it can show. Nought while nothing is known.
    [[nodiscard]] double arrivalsPerSecond() const noexcept;

    /// Reads now. **What the timer calls**, and what a test calls instead.
    void tick();

    /// Starts and stops the timer: a feed with nobody looking does nothing.
    void start();
    void stop();
    [[nodiscard]] bool isRunning() const
    {
        return m_timer.isActive();
    }

    /// What is in memory is another session's, or is going.
    void forget();

Q_SIGNALS:
    /// A look brought something, or the trace may no longer be shown.
    void refreshed();
    void stateChanged();

private:
    /// What one surface said.
    struct Said {
        const void *surface = nullptr;
        ScopeSettings settings;
        bool hold = false;
    };

    void setState(State state);
    void read(const rt::ScopeSlot &slot, double cycleMs, std::uint32_t sampleRate);

    ScopeHub *m_hub;
    QString m_blockId;
    QTimer m_timer;
    ScopeSettings m_settings;
    bool m_hold = false;
    State m_state = State::Waiting;
    ScopeFrame m_frame;
    SpectrumAnalysis m_spectrum;

    /// Oldest first: the last is what counts.
    std::vector<Said> m_said;
    bool m_seenSlot = false;
    int m_slot = -1;
    std::uint32_t m_generation = 0;
    std::array<std::uint32_t, rt::kScopeLevels> m_lastHead{};
    std::array<std::uint64_t, rt::kScopeLevels> m_wraps{};
    std::size_t m_level = 0;
    qint64 m_wantedSince = 0;
    qint64 m_movedAt = 0;
    qint64 m_renewedAt = -1;
};

/// **Where surfaces get the sound they draw.** A surface subscribes for as long
/// as it is showing, and the hub keeps one feed a block.
///
/// The sound itself takes no route through here: a feed reads the slot the
/// audio thread writes (ADR-0018), and the hub's part is who is looking.
class ScopeHub : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("the hub comes from Meters.scopes")

public:
    /// How a block is asked for. Called about once a second per block.
    using Asker = std::function<void(const QString &sessionId, const QString &blockId)>;

    explicit ScopeHub(const MeterHub *meters, QObject *parent = nullptr);
    ~ScopeHub() override;

    /// Reads \a pool, \a sessionId's, from now on. Null is no session, or
    /// memory not mapped yet. The pool must outlive the hub or be taken away
    /// with another call.
    void watch(const QString &sessionId, const rt::ScopePool *pool);

    /// The feed of \a blockId, made if need be. Say unsubscribe() as many
    /// times.
    [[nodiscard]] ScopeFeed *subscribe(const QString &blockId);
    void unsubscribe(ScopeFeed *feed);

    /// How many blocks are being drawn, for a test.
    [[nodiscard]] Q_INVOKABLE int feedCount() const noexcept
    {
        return static_cast<int>(m_feeds.size());
    }

    void setAsker(Asker asker);
    /// The clock everything here goes by, in milliseconds. A test has its own.
    void setClock(std::function<double()> clock);
    [[nodiscard]] double nowMs() const;

private:
    friend class ScopeFeed;

    struct Subscribed {
        std::unique_ptr<ScopeFeed> feed;
        int count = 0;
    };

    const MeterHub *m_meters;
    QString m_sessionId;
    const rt::ScopePool *m_pool = nullptr;
    std::map<QString, Subscribed> m_feeds;
    Asker m_asker;
    std::function<double()> m_clock;
};

} // namespace rb::ui
