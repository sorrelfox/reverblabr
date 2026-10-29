// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/compositor.h"

#include "stream/textpainter.h"
#include "stream/visuals.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rb::stream
{

namespace
{

/// The two colours words can be: light over what is dark, dark over what
/// is light.
constexpr Rgb kLightText{0xf4, 0xf5, 0xf7};
constexpr Rgb kDarkText{0x1b, 0x1e, 0x24};

/// Above this, what is behind the words counts as light. A little over
/// half, because light words on a middle grey read better than dark ones.
constexpr double kLightBehind = 0.58;

} // namespace

struct Compositor::Private {
    int width = 0;
    int height = 0;
    TextPainter *painter = nullptr;
    Scene scene;
    bool sceneSet = false;
    /// What stands still, drawn once a scene.
    Canvas still;
    /// The frame handed out, drawn over a copy of the still layer.
    Canvas frame;
    std::optional<Picture> background;
    std::optional<Picture> logo;
    Rgb text = kLightText;
    Rgb shade = kDarkText;

    [[nodiscard]] int margin() const noexcept
    {
        return std::max(4, height / 20);
    }

    [[nodiscard]] Rect logoArea() const noexcept
    {
        return Rect{margin(), margin(), width * 3 / 10, height * 4 / 25};
    }

    [[nodiscard]] Rect titleArea() const noexcept
    {
        return Rect{margin(), height * 6 / 25, width - margin() * 2, height * 19 / 50};
    }

    [[nodiscard]] Rect visualArea() const noexcept
    {
        return Rect{margin() * 2, height * 33 / 50, width - margin() * 4, height * 6 / 25};
    }

    /// \a line centred across the frame, its top at \a top, with a shade
    /// around it.
    void drawCentred(const TextLine &line, int top)
    {
        if (line.coverage.empty()) {
            return;
        }
        const int left = (width - line.coverage.width) / 2;
        // **A shade a little way round the words**, so that a busy
        // picture behind them does not swallow them.
        const int offset = std::max(1, line.pixelSize / 28);
        for (const auto &[dx, dy] : {std::pair{offset, offset},
                                     std::pair{-offset, offset},
                                     std::pair{offset, -offset},
                                     std::pair{-offset, -offset}}) {
            still.paint(line.coverage, left + dx, top + dy, shade, 0.28);
        }
        still.paint(line.coverage, left, top, text);
    }

    void drawStill()
    {
        still.fill(scene.background);
        if (background) {
            still.draw(*background, (width - background->width) / 2, (height - background->height) / 2);
        }
        if (logo) {
            const Rect area = logoArea();
            still.draw(*logo, area.x, area.y + (area.height - logo->height) / 2);
        }
        // The words' colour from what is behind them, before they are on it.
        const bool lightBehind = still.luma(titleArea()) > kLightBehind;
        text = lightBehind ? kDarkText : kLightText;
        shade = lightBehind ? kLightText : kDarkText;
        if (!painter) {
            return;
        }
        const int room = width - margin() * 4;
        const Rect area = titleArea();
        int top = area.y;
        const TextLine title = painter->set(scene.title, height * 17 / 200, true, room, height / 25);
        if (!title.coverage.empty()) {
            drawCentred(title, top);
            top += title.coverage.height;
        }
        const TextLine subtitle = painter->set(scene.subtitle, height * 9 / 200, false, room, height / 40);
        if (!subtitle.coverage.empty()) {
            drawCentred(subtitle, top);
            top += subtitle.coverage.height;
        }
        // What is playing sits at the bottom of the words' space, so that it
        // does not jump about as a subtitle comes and goes.
        const TextLine track = painter->set(scene.track, height * 21 / 500, false, room, height / 40);
        if (!track.coverage.empty()) {
            drawCentred(track, std::max(top, area.y + area.height - track.coverage.height));
        }
    }

    void drawWaveform(const AudioVisuals &visuals, const Rect &area)
    {
        if (visuals.waveform.empty() || area.width <= 0) {
            return;
        }
        const int middle = area.y + area.height / 2;
        const double half = area.height / 2.0;
        const int thickness = std::max(2, height / 270);
        int lastY = middle;
        for (int column = 0; column < area.width; ++column) {
            const std::size_t at =
                static_cast<std::size_t>(column) * visuals.waveform.size() / static_cast<std::size_t>(area.width);
            const double value = std::clamp(static_cast<double>(visuals.waveform[at]), -1.0, 1.0);
            const int y = middle - static_cast<int>(std::lround(value * half));
            // **Joined to the column before**, so a steep wave is a line and
            // not a scatter of dots.
            const int from = column == 0 ? y : std::min(lastY, y);
            const int to = column == 0 ? y : std::max(lastY, y);
            frame.fillRect(Rect{area.x + column, from - thickness / 2, 1, to - from + thickness}, text, 0.9);
            lastY = y;
        }
    }

    void drawSpectrum(const AudioVisuals &visuals, const Rect &area)
    {
        const std::size_t bands = visuals.spectrum.size();
        if (bands == 0 || area.width <= 0) {
            return;
        }
        const double step = static_cast<double>(area.width) / static_cast<double>(bands);
        const int gap = std::max(1, static_cast<int>(step / 4.0));
        for (std::size_t band = 0; band < bands; ++band) {
            const int left = area.x + static_cast<int>(std::lround(step * static_cast<double>(band)));
            const int right = area.x + static_cast<int>(std::lround(step * static_cast<double>(band + 1))) - gap;
            const int tall = std::max(1, static_cast<int>(std::lround(visuals.spectrum[band] * area.height)));
            frame.fillRect(Rect{left, area.y + area.height - tall, std::max(1, right - left), tall}, text, 0.85);
        }
    }

    void drawMeter(const AudioVisuals &visuals, const Rect &area)
    {
        const int bar = std::max(4, area.height / 5);
        const int gap = bar / 2;
        const int top = area.y + (area.height - bar * 2 - gap) / 2;
        for (std::size_t side = 0; side < 2; ++side) {
            const int y = top + static_cast<int>(side) * (bar + gap);
            frame.fillRect(Rect{area.x, y, area.width, bar}, text, 0.15);
            frame.fillRect(
                Rect{area.x, y, static_cast<int>(std::lround(visuals.level[side] * area.width)), bar}, text, 0.85);
            const int peak = area.x + static_cast<int>(std::lround(visuals.peak[side] * area.width));
            frame.fillRect(Rect{std::min(peak, area.x + area.width - 3), y, 3, bar}, text);
        }
    }
};

Compositor::Compositor(int width, int height, TextPainter *painter)
    : d(std::make_unique<Private>())
{
    d->width = std::max(width, 16);
    d->height = std::max(height, 16);
    d->painter = painter;
    d->still = Canvas(d->width, d->height);
    d->frame = Canvas(d->width, d->height);
    d->drawStill();
}

Compositor::~Compositor() = default;

int Compositor::width() const noexcept
{
    return d->width;
}

int Compositor::height() const noexcept
{
    return d->height;
}

void Compositor::setScene(const Scene &scene)
{
    if (d->sceneSet && scene == d->scene) {
        return;
    }
    if (!d->sceneSet || scene.backgroundImage != d->scene.backgroundImage) {
        d->background = loadPicture(scene.backgroundImage, d->width, d->height, Fit::Cover);
    }
    if (!d->sceneSet || scene.logo != d->scene.logo) {
        const Rect area = d->logoArea();
        d->logo = loadPicture(scene.logo, area.width, area.height, Fit::Contain);
    }
    d->scene = scene;
    d->sceneSet = true;
    d->drawStill();
}

const Scene &Compositor::scene() const noexcept
{
    return d->scene;
}

const Canvas &Compositor::render(const AudioVisuals &visuals)
{
    d->frame.copyFrom(d->still);
    const Rect area = d->visualArea();
    switch (d->scene.visualisation) {
    case Visualisation::None:
        break;
    case Visualisation::Waveform:
        d->drawWaveform(visuals, area);
        break;
    case Visualisation::Spectrum:
        d->drawSpectrum(visuals, area);
        break;
    case Visualisation::Meter:
        d->drawMeter(visuals, area);
        break;
    }
    return d->frame;
}

Rgb Compositor::textColour() const noexcept
{
    return d->text;
}

Rect Compositor::titleArea() const noexcept
{
    return d->titleArea();
}

Rect Compositor::visualArea() const noexcept
{
    return d->visualArea();
}

Rect Compositor::logoArea() const noexcept
{
    return d->logoArea();
}

} // namespace rb::stream
