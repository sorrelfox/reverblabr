// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/canvas.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace rb::stream
{

struct AudioVisuals;
class TextPainter;

/// What moves with the sound, under the words.
enum class Visualisation : std::uint8_t {
    None,
    /// The sound's wave, as an oscilloscope draws it.
    Waveform,
    /// Bars, one for each band of frequencies.
    Spectrum,
    /// Each side's level as a bar across, with its peak.
    Meter,
};

/// **What the picture is made of**, everything but the sound. A change to
/// any of it redraws the part that stands still once; the frames in
/// between only draw what moves.
struct Scene {
    std::string title;
    std::string subtitle;
    /// What is playing, as the title template made it. Empty for nothing.
    std::string track;
    Rgb background{0x16, 0x18, 0x1d};
    /// A picture under everything, covering the frame. None when empty.
    std::filesystem::path backgroundImage;
    /// A picture at the top left, such as a show's logo. None when empty.
    std::filesystem::path logo;
    Visualisation visualisation = Visualisation::Waveform;

    [[nodiscard]] bool operator==(const Scene &other) const = default;
};

/// **Draws the frames of a stream's video**, on the stream's thread: a
/// background, a logo, a title, a subtitle, what is playing, and something
/// that moves with the sound.
///
/// **The part that stands still is drawn once**, into a layer of its own,
/// when the scene changes; each frame copies it and draws the
/// visualisation over it. **The text's colour is chosen by what is behind
/// it**, light over a dark picture and dark over a light one, with a shade
/// around it so that a busy picture does not swallow it.
///
/// The layout is in proportion to the frame, so the same scene looks the
/// same at 576, 720 and 1080 lines.
class Compositor
{
public:
    /// A compositor for frames of \a width × \a height. \a painter sets
    /// the words, and is the caller's: it outlives the compositor, and the
    /// next one made for another size takes the same. Without one (a
    /// system with no fonts) the frames have no words.
    Compositor(int width, int height, TextPainter *painter);
    ~Compositor();
    Compositor(const Compositor &) = delete;
    Compositor &operator=(const Compositor &) = delete;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    /// What to draw from now on. **Pictures are read here**, when their
    /// file changes, and not again while it does not.
    void setScene(const Scene &scene);
    [[nodiscard]] const Scene &scene() const noexcept;

    /// The next frame, with \a visuals in it.
    [[nodiscard]] const Canvas &render(const AudioVisuals &visuals);

    /// The colour the words are drawn in now: chosen by what is behind
    /// them.
    [[nodiscard]] Rgb textColour() const noexcept;

    /// Where the parts of the picture go, in proportion to the frame;
    /// for a test to look at what was drawn where.
    [[nodiscard]] Rect titleArea() const noexcept;
    [[nodiscard]] Rect visualArea() const noexcept;
    [[nodiscard]] Rect logoArea() const noexcept;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::stream
