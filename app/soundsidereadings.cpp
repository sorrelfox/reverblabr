// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "soundsidereadings.h"

#include "appshell.h"

#include <QSet>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

using State = blocks::SoundSide::State;
using Word = SoundSideReading::Word;

[[nodiscard]] QString key(const QString &sessionId, const QString &blockId)
{
    return sessionId + u'/' + blockId;
}

[[nodiscard]] QString kindOf(Word word)
{
    switch (word) {
    case Word::Left:
        return u"left"_s;
    case Word::Right:
        return u"right"_s;
    case Word::Centre:
        return u"centre"_s;
    case Word::Both:
        return u"both"_s;
    case Word::Same:
        return u"same"_s;
    case Word::Quiet:
        break;
    }
    return u"quiet"_s;
}

[[nodiscard]] QVariantMap
asMap(const QString &kind, const QString &watch, const SoundSideReading::Shown &shown, float level)
{
    QVariantList ghosts;
    for (const SoundSideReading::Ghost &ghost : shown.ghosts) {
        ghosts.append(QVariantMap{{u"pan"_s, ghost.pan}, {u"strength"_s, ghost.strength}});
    }
    return QVariantMap{
        {u"kind"_s, kind},
        {u"watch"_s, watch},
        {u"pan"_s, shown.pan},
        {u"strength"_s, shown.strength},
        {u"opening"_s, shown.opening},
        {u"ghosts"_s, ghosts},
        // A whole decibel is all that is written, and noise moves the rest.
        {u"level"_s, static_cast<int>(std::lround(level))},
        {u"markLevel"_s, static_cast<int>(std::lround(shown.markLevel))},
        {u"percent"_s, static_cast<int>(std::lround(std::abs(shown.pan) * 100.0f))},
        {u"oneSideOnly"_s, shown.oneSideOnly},
    };
}

[[nodiscard]] QVariantMap notHeard(const QString &kind, const QString &watch)
{
    return asMap(kind, watch, SoundSideReading::Shown{}, blocks::SoundSide::kNoLevel);
}

} // namespace

Word SoundSideReading::heardWord(float pan, float width) const
{
    const bool wasOnThisSide = (pan < 0.0f && m_heard == Word::Left) || (pan > 0.0f && m_heard == Word::Right);
    if (std::abs(pan) >= (wasOnThisSide ? kBackAtPan : kSideAtPan)) {
        return pan < 0.0f ? Word::Left : Word::Right;
    }
    return width >= kBothBelowWidth ? Word::Centre : Word::Both;
}

void SoundSideReading::followSound(qint64 nowMs, float pan, float level, float width)
{
    // Only time with a mark up counts: a first sighting has no time behind it.
    const bool wasMarked = m_marked;
    m_marked = true;
    m_lastSoundMs = nowMs;
    m_shown.pan = pan;
    m_shown.strength = 1.0f;
    m_shown.markLevel = level;
    // Sound on one side has nothing to correlate with, and is one place.
    m_shown.opening = (1.0f - std::clamp(width, 0.0f, 1.0f)) * (1.0f - std::abs(pan));
    m_past[m_pastNext] = {nowMs, pan};
    m_pastNext = (m_pastNext + 1) % m_past.size();
    m_pastCount = std::min(m_pastCount + 1, m_past.size());

    if (!wasMarked || std::abs(pan) < kOneSidePan || pan * m_oneSide < 0.0f) {
        m_oneSideMs = 0;
    } else {
        m_oneSideMs = std::min(m_oneSideMs + nowMs - m_lastSeenMs, kOneSideOnlyMs);
    }
    m_oneSide = pan;
    m_shown.oneSideOnly = m_oneSideMs >= kOneSideOnlyMs;
}

void SoundSideReading::letGo(qint64 nowMs)
{
    const qint64 age = nowMs - m_lastSoundMs;
    if (age >= kHoldMs + kFadeMs) {
        m_marked = false;
        m_pastCount = 0;
        m_shown.strength = 0.0f;
        return;
    }
    if (age > kHoldMs) {
        m_shown.strength = 1.0f - static_cast<float>(age - kHoldMs) / static_cast<float>(kFadeMs);
    }
}

void SoundSideReading::settleWord(qint64 nowMs, Word heard)
{
    m_heard = heard;
    if (heard == m_shown.word) {
        m_waiting = heard;
        return;
    }
    if (heard != m_waiting) {
        m_waiting = heard;
        m_waitingSince = nowMs;
    }
    if (nowMs - m_waitingSince >= kWordHoldsMs) {
        m_shown.word = heard;
    }
}

void SoundSideReading::drawTrail(qint64 nowMs)
{
    for (std::size_t ghost = 0; ghost < m_shown.ghosts.size(); ++ghost) {
        Ghost drawn;
        // The newest place at least this old. Older ones are further round
        // the ring, and a place the mark still stands on needs no ghost.
        for (std::size_t back = 1; m_marked && back <= m_pastCount; ++back) {
            const Past &past = m_past[(m_pastNext + m_past.size() - back) % m_past.size()];
            if (nowMs - past.atMs >= kGhostAgesMs[ghost]) {
                if (past.pan != m_shown.pan) {
                    drawn = {past.pan, kGhostStrengths[ghost] * m_shown.strength};
                }
                break;
            }
        }
        m_shown.ghosts[ghost] = drawn;
    }
}

void SoundSideReading::saw(qint64 nowMs, float pan, float level, float width, State state)
{
    if (state == State::SidesAlike) {
        // Nothing to mark: the track empties, and the word needs its time.
        m_marked = false;
        m_pastCount = 0;
        m_shown.strength = 0.0f;
        settleWord(nowMs, Word::Same);
    } else if (state == State::Sounding) {
        const Word heard = heardWord(pan, width);
        followSound(nowMs, pan, level, width);
        settleWord(nowMs, heard);
    } else if (m_marked) {
        letGo(nowMs);
        if (m_marked) {
            settleWord(nowMs, m_heard);
        }
    }
    if (state == State::Quiet && !m_marked) {
        m_heard = Word::Quiet;
        m_waiting = Word::Quiet;
        m_shown.word = Word::Quiet;
    }
    if (!m_marked) {
        // The sound has gone, so the line goes, and the next one counts afresh.
        m_oneSideMs = 0;
        m_shown.oneSideOnly = false;
    }
    drawTrail(nowMs);
    m_lastSeenMs = nowMs;
}

void SoundSideReading::reset()
{
    *this = SoundSideReading{};
}

SoundSideReadings::SoundSideReadings(QObject *parent)
    : QObject(parent)
{
    m_sinceStart.start();
    m_ticker.setInterval(kTickMs);
    m_ticker.setTimerType(Qt::PreciseTimer);
    connect(&m_ticker, &QTimer::timeout, this, &SoundSideReadings::read);
}

SoundSideReadings::~SoundSideReadings() = default;

int SoundSideReadings::revision() const
{
    return m_revision;
}

void SoundSideReadings::hold(const QString &sessionId, const QString &blockId)
{
    if (sessionId.isEmpty() || blockId.isEmpty()) {
        return;
    }
    Entry &entry = m_entries[key(sessionId, blockId)];
    entry.sessionId = sessionId;
    entry.blockId = blockId;
    ++entry.holders;
    // The next tick reads it: this is called from a binding's change, and a
    // read here would say `changed` from inside that binding.
    if (!m_ticker.isActive()) {
        m_ticker.start();
    }
}

void SoundSideReadings::release(const QString &sessionId, const QString &blockId)
{
    const auto found = m_entries.find(key(sessionId, blockId));
    if (found == m_entries.end() || --found->holders > 0) {
        return;
    }
    m_entries.erase(found);
    if (m_entries.isEmpty()) {
        m_ticker.stop();
        m_watches.clear();
    }
}

QVariantMap SoundSideReadings::shown(const QString &sessionId, const QString &blockId) const
{
    const auto found = m_entries.constFind(key(sessionId, blockId));
    return found != m_entries.cend() && !found->shown.isEmpty()
        ? found->shown
        : notHeard(u"stopped"_s, QString(TelemetryWatch::kStopped));
}

void SoundSideReadings::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

void SoundSideReadings::read()
{
    const qint64 now = m_clock ? m_clock() : m_sinceStart.elapsed();
    QHash<QString, std::optional<core::TelemetrySnapshot>> snapshots;
    bool moved = false;
    for (Entry &entry : m_entries) {
        auto snapshot = snapshots.find(entry.sessionId);
        if (snapshot == snapshots.end()) {
            // **One read a session**, however many blocks it has.
            const core::TelemetrySlot *slot = AppShell::telemetryOf(entry.sessionId);
            snapshot = snapshots.insert(entry.sessionId, slot != nullptr ? std::optional(slot->load()) : std::nullopt);
            if (*snapshot) {
                m_watches[entry.sessionId].saw(**snapshot, now);
            } else {
                m_watches.remove(entry.sessionId);
            }
        }
        const core::BlockTelemetry *published = *snapshot ? TelemetryWatch::find(**snapshot, entry.blockId) : nullptr;
        const auto watch = m_watches.constFind(entry.sessionId);
        const QString said = watch != m_watches.cend() ? QString(watch->stateOf(published != nullptr, now))
                                                       : QString(TelemetryWatch::kStopped);
        QVariantMap shown;
        if (said != TelemetryWatch::kWatching || published == nullptr) {
            entry.reading.reset();
            shown = notHeard(u"stopped"_s, said);
        } else if (!published->enabled) {
            // A block off is not run, and its slots say what they said last.
            entry.reading.reset();
            shown = notHeard(u"off"_s, said);
        } else {
            const float level = published->values[blocks::SoundSide::kLevelValue];
            entry.reading.saw(now,
                              published->values[blocks::SoundSide::kPanValue],
                              level,
                              published->values[blocks::SoundSide::kWidthValue],
                              static_cast<State>(std::lround(published->values[blocks::SoundSide::kStateValue])));
            shown = asMap(kindOf(entry.reading.shown().word), said, entry.reading.shown(), level);
        }
        if (shown != entry.shown) {
            entry.shown = std::move(shown);
            moved = true;
        }
    }
    if (moved) {
        ++m_revision;
        Q_EMIT changed();
    }
}

} // namespace rb::ui
