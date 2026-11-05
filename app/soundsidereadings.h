// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "blocks/soundside/soundside.h"
#include "telemetrywatch.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <array>
#include <cstddef>
#include <functional>

namespace rb::ui
{

/// **What one Sound Side shows, from the four numbers it publishes:** the
/// word, the mark, its hold and fade, and the trail behind it. Clock in hand,
/// nothing to do with where any of it is drawn.
///
/// **The way in is never smoothed.** A mark goes where the sound is at once,
/// and hold and fade act only on the way out. A word has to stand for
/// kWordHoldsMs first, so it cannot flicker; the mark does not wait for it.
///
/// **Quiet comes when the mark has gone,** so a word never disagrees with what
/// is on the track, and a click too short to hold a word by itself still does,
/// for as long as its mark stands.
class SoundSideReading
{
public:
    static constexpr qint64 kHoldMs = 250;
    static constexpr qint64 kFadeMs = 250;
    static constexpr qint64 kWordHoldsMs = 100;
    static constexpr qint64 kOneSideOnlyMs = 10000;
    /// A side is left at this pan and comes back under kBackAtPan.
    static constexpr float kSideAtPan = 0.25f;
    static constexpr float kBackAtPan = 0.20f;
    static constexpr float kBothBelowWidth = 0.5f;
    static constexpr float kOneSidePan = 0.99f;
    /// The trail's three ghosts: how old, and how strong.
    static constexpr std::array<qint64, 3> kGhostAgesMs{60, 120, 180};
    static constexpr std::array<float, 3> kGhostStrengths{0.45f, 0.28f, 0.14f};

    enum class Word {
        Quiet,
        Left,
        Right,
        Centre,
        Both,
        Same,
    };

    struct Ghost {
        float pan = 0.0f;
        float strength = 0.0f; ///< Nought for no ghost.
    };

    struct Shown {
        Word word = Word::Quiet;
        /// Where the mark is. **With strength nought there is no mark**, and
        /// the pan means nothing: silence must never read as the middle.
        float pan = 0.0f;
        float strength = 0.0f;
        /// How far the mark has opened, 0 narrow to 1 half the track.
        float opening = 0.0f;
        /// The level of the sound the mark stands for.
        float markLevel = blocks::SoundSide::kNoLevel;
        std::array<Ghost, 3> ghosts{};
        /// The sound has stayed on one side alone for kOneSideOnlyMs.
        bool oneSideOnly = false;
    };

    /// What a read of the block found at \a nowMs.
    void saw(qint64 nowMs, float pan, float level, float width, blocks::SoundSide::State state);
    /// Starts clean: the block can't be heard, so nothing of it stands.
    void reset();

    [[nodiscard]] const Shown &shown() const
    {
        return m_shown;
    }

private:
    struct Past {
        qint64 atMs = 0;
        float pan = 0.0f;
    };

    [[nodiscard]] Word heardWord(float pan, float width) const;
    void followSound(qint64 nowMs, float pan, float level, float width);
    void letGo(qint64 nowMs);
    void settleWord(qint64 nowMs, Word heard);
    void drawTrail(qint64 nowMs);

    Shown m_shown;
    /// The word the sound says, before it has stood long enough to show.
    Word m_heard = Word::Quiet;
    Word m_waiting = Word::Quiet;
    qint64 m_waitingSince = 0;
    qint64 m_lastSoundMs = 0;
    bool m_marked = false;
    qint64 m_lastSeenMs = 0;
    qint64 m_oneSideMs = 0;
    float m_oneSide = 0.0f;
    std::array<Past, 16> m_past{};
    std::size_t m_pastCount = 0;
    std::size_t m_pastNext = 0;
};

/// **One SoundSideReading a block, for every surface alike**, so that a tile,
/// a card and a strip can't disagree: each asks here, and none works a word
/// out for itself.
///
/// **Its own read, sixty times a second, while somebody holds a block.** The
/// session a strip shows need not be the one in the window, and `Meters` runs
/// slower than the mark does. `changed` is said only when something shown
/// differs, so a strip over a game with nothing playing asks for no frames.
class SoundSideReadings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Counts up when anything shown changes, for bindings to depend on.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    static constexpr int kTickMs = 16;

    explicit SoundSideReadings(QObject *parent = nullptr);
    ~SoundSideReadings() override;

    [[nodiscard]] int revision() const;

    /// Somebody shows this block from now on, until they release() it. Counted,
    /// as a tile, a card and a strip may hold one block between them.
    Q_INVOKABLE void hold(const QString &sessionId, const QString &blockId);
    Q_INVOKABLE void release(const QString &sessionId, const QString &blockId);

    /// What to show, as `{kind, watch, pan, strength, opening, ghosts, level,
    /// markLevel, percent, oneSideOnly}`. `kind` is "quiet", "left", "right",
    /// "centre", "both", "same", "stopped" or "off"; `watch` a word of
    /// TelemetryWatch's; `ghosts` a list of `{pan, strength}`.
    [[nodiscard]] Q_INVOKABLE QVariantMap shown(const QString &sessionId, const QString &blockId) const;

    /// Reads the telemetry once, now: what the timer does.
    void read();
    /// The clock the hold and the fade are timed against, for a test.
    void setClock(std::function<qint64()> clock);

Q_SIGNALS:
    void changed();

private:
    struct Entry {
        QString sessionId;
        QString blockId;
        int holders = 0;
        SoundSideReading reading;
        QVariantMap shown;
    };

    QHash<QString, Entry> m_entries;
    QHash<QString, TelemetryWatch> m_watches;
    QTimer m_ticker;
    QElapsedTimer m_sinceStart;
    std::function<qint64()> m_clock;
    int m_revision = 0;
};

} // namespace rb::ui
