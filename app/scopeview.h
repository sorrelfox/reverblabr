// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/scopetrace.h"
#include "rt/scope.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::ui
{

/// What a surface was asked to show: a Waveform block's settings as they
/// reach the drawing.
struct ScopeSettings {
    enum class Channels : std::uint8_t {
        Both, ///< Left and right over each other.
        Left,
        Right,
        Mid,
        Side,
        Stacked, ///< Left above right.
    };

    double refreshHz = 30.0;
    bool smooth = false;
    double spanMs = 200.0;
    double gainDb = 0.0;
    bool autoFit = false;
    Channels channels = Channels::Both;
    bool trigger = false;
    bool decibels = false;
};

/// **The points of one look at a slot**, kept by ScopeFeed for every surface
/// showing the block. Positions are counted in samples since the slot was
/// given out, not in points, so that nothing has to change when a longer view
/// reads from a coarser ring.
struct ScopeFrame {
    std::vector<rt::ScopePoint> points; ///< Oldest first.
    /// How many samples had gone by after the last point.
    double newestFrame = 0.0;
    std::uint32_t framesPerPoint = 1;
    std::uint32_t sampleRate = 0;
    /// Counts up with each look that brought something.
    std::uint64_t serial = 0;
    /// How much sound a cycle brings, in milliseconds, so that a smooth view
    /// keeps far enough behind.
    double cycleMs = 0.0;
    /// Counts up with each slot given out: what came before is another sound.
    std::uint64_t run = 0;
};

/// **The time a smooth view shows, running by itself.**
///
/// News of the sound arrives in lumps: a refresh's worth at a time, and never
/// less than a cycle's. A view that jumped to each lump would move as often
/// as they come. So the time on the screen is a clock of its own that runs at
/// the sample rate, a little behind the newest sound, and is drawn at every
/// frame the screen has.
///
/// **It is steered very gently**, over a couple of seconds, because a lump
/// arriving is a step in where the clock should be: followed closely, the
/// trace would speed up and slow down with each one, which is the jerk this
/// exists to take away.
class ScopeClock
{
public:
    /// How gently, in milliseconds.
    static constexpr double kSteerMs = 2000.0;

    /// The time to show at \a nowMs, in samples, for sound known up to
    /// \a newestFrame. \a lagFrames is how far behind it to stay.
    [[nodiscard]] double advance(double newestFrame, double sampleRate, double lagFrames, double nowMs) noexcept;

    /// Starts again from wherever the sound is.
    void restart() noexcept
    {
        m_running = false;
    }

private:
    bool m_running = false;
    double m_position = 0.0;
    double m_lastMs = 0.0;
};

/// **What one surface draws**, worked out from a frame: which stretch of the
/// sound, in which columns, at what height. Knows no window, so a test can
/// run it with a clock of its own. One a surface, since a tile and a card have
/// different widths.
///
/// **What Real-time and Smooth mean here.** Real-time works out a picture when
/// a refresh brings news, of the newest sound exactly as it is, and leaves it
/// until the next. Smooth works one out at every frame the screen draws, and
/// either way the picture never jumps:
///
/// - **Long views scroll.** From kScrollFromMs up, the stretch shown slides
///   along with ScopeClock, a fraction of a column at a time.
/// - **Short views settle.** Below that, or with the trigger on, there is
///   nothing to follow: a 10 ms view moves on by more than its own width each
///   frame. So each new picture is eased into from the last. And because two
///   tones at different places in their cycle ease into nothing, a short
///   smooth view is lined up on a rising crossing whether or not the trigger
///   was asked for.
///
/// **Columns stand still on the sound.** A column's edges are at whole
/// multiples of its own length counted from the start of the sound, not from
/// the edge of the view, so a column holds the same samples as it travels and
/// the shape does not shimmer as it scrolls. Hence shift().
class ScopeView
{
public:
    /// The shortest span that scrolls when Smooth.
    static constexpr double kScrollFromMs = 200.0;
    /// How much further back than it shows a view reads for a crossing.
    static constexpr double kLookBackMs = 100.0;
    /// How long a short smooth view takes to settle at the least, in
    /// milliseconds, so that Smooth is smooth at any refresh rate.
    static constexpr double kLeastSettleMs = 60.0;
    /// The Decibels scale's bottom.
    static constexpr double kDecibelFloor = -60.0;
    /// What Auto fit fills the height to, and how fast it lets go, in ms.
    static constexpr double kFitTo = 0.9;
    static constexpr double kFitLetGoMs = 1000.0;

    /// The most traces that are drawn at once.
    static constexpr std::size_t kMaxTraces = 2;

    /// Which samples a frame should hold for these \a settings, so that
    /// ScopeFeed can read as little as will do.
    [[nodiscard]] static double framesWanted(const ScopeSettings &settings, double sampleRate, double cycleMs);

    /// The traces \a settings shows, in the order they are drawn.
    [[nodiscard]] static std::size_t traceCount(ScopeSettings::Channels channels) noexcept;
    [[nodiscard]] static std::size_t traceOf(ScopeSettings::Channels channels, std::size_t index) noexcept;

    /// Whether these settings ask to be drawn at every frame of the screen.
    [[nodiscard]] static bool movesBetweenRefreshes(const ScopeSettings &settings) noexcept
    {
        return settings.smooth;
    }

    /// Works out the picture at \a nowMs from \a frame in \a columns columns.
    /// Without news a Real-time view stays as it was. Returns whether the
    /// picture changed.
    bool update(const ScopeFrame &frame, const ScopeSettings &settings, std::size_t columns, double nowMs);

    /// Forgets the picture: a view starting again shows no easing from the
    /// last thing it saw.
    void clear() noexcept;

    /// How many columns there are, and how far to the left they stand of
    /// where they would be, as a share of one: nought to one.
    [[nodiscard]] std::size_t columns() const noexcept
    {
        return m_columns;
    }
    [[nodiscard]] double shift() const noexcept
    {
        return m_shift;
    }
    /// Which column the first one is, counted from the start of the sound:
    /// what lets a bar keep its place on the sound as it travels.
    [[nodiscard]] std::uint64_t firstColumn() const noexcept
    {
        return m_firstColumn;
    }

    /// How many traces are drawn now.
    [[nodiscard]] std::size_t traces() const noexcept
    {
        return m_traces;
    }

    /// The columns of one trace, as heights from -1 to 1 with the view's gain
    /// and scale applied. A column not \a heard has nothing to draw.
    [[nodiscard]] std::span<const dsp::ScopeColumn> trace(std::size_t index) const noexcept
    {
        return index < kMaxTraces ? std::span<const dsp::ScopeColumn>(m_shown[index])
                                  : std::span<const dsp::ScopeColumn>();
    }

    /// The sample the right-hand edge shows.
    [[nodiscard]] double endFrame() const noexcept
    {
        return m_endFrame;
    }
    /// Whether this picture started on a crossing.
    [[nodiscard]] bool crossed() const noexcept
    {
        return m_crossed;
    }
    /// What Auto fit has enlarged it by now, or the block's gain, in dB.
    [[nodiscard]] double gainDb() const noexcept
    {
        return m_gainDb;
    }

private:
    /// Where the view starts, as a position among \a frame's points.
    [[nodiscard]] double
    locate(const ScopeFrame &frame, const ScopeSettings &settings, double spanPoints, double nowMs);
    void fit(const ScopeSettings &settings, double dtMs);
    void scale(const ScopeSettings &settings);

    ScopeClock m_clock;
    std::size_t m_columns = 0;
    std::size_t m_traces = 0;
    double m_shift = 0.0;
    std::uint64_t m_firstColumn = 0;
    double m_endFrame = 0.0;
    double m_gainDb = 0.0;
    double m_lastMs = 0.0;
    bool m_crossed = false;
    bool m_started = false;
    std::uint64_t m_seenSerial = 0;
    std::uint64_t m_seenRun = 0;
    ScopeSettings m_seen;
    std::array<std::vector<dsp::ScopeColumn>, kMaxTraces> m_raw;
    std::array<std::vector<dsp::ScopeColumn>, kMaxTraces> m_target;
    std::array<std::vector<dsp::ScopeColumn>, kMaxTraces> m_shown;
};

} // namespace rb::ui
