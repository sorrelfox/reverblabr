// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "editor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace rb::pluginhost
{

/// Where and how large a plugin's window last was, on the screen.
struct WindowPlace {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(const WindowPlace &other) const = default;
};

/// **Where each plugin's window was last**, by a key that says which plugin
/// ("lv2 <uri>", "clap <id>"): kept in a file of its own under the user's
/// state folder, so a plugin's window opens where it was closed, in any
/// session. The host's main thread only.
[[nodiscard]] std::optional<WindowPlace> placeOf(const std::string &key);
void rememberPlace(const std::string &key, const WindowPlace &place);
/// The file the places are kept in: `$XDG_STATE_HOME/reverblabr/plugin-windows`,
/// or under `~/.local/state` when that is not set.
[[nodiscard]] std::string placesFile();

/// Whether there is an X display to show a window on: on a Wayland desktop,
/// XWayland's.
[[nodiscard]] bool haveDisplay();

/// **A top-level window of the host's own**, for a plugin whose window is
/// made inside a parent the host gives it: an LV2 X11 UI, or a CLAP window
/// that will not float. On a Wayland desktop it is an XWayland window.
///
/// It goes where the plugin's window was last, and says so when a person
/// closes it. Its events are read on the host's main loop.
class X11Window
{
public:
    /// A window titled \a title, at the place kept for \a key or at
    /// \a width by \a height; nothing when there is no display. \a closed
    /// is told when a person closes it, or with true when the connection to
    /// the display was lost (the host goes on without the window);
    /// \a resized when they resize it.
    [[nodiscard]] static std::unique_ptr<X11Window> open(MainLoop &loop,
                                                         const std::string &title,
                                                         const std::string &key,
                                                         int width,
                                                         int height,
                                                         std::function<void(bool)> closed,
                                                         std::function<void(int, int)> resized = {});

    /// Keeps where it is for next time, and takes it off the screen.
    ~X11Window();
    X11Window(const X11Window &) = delete;
    X11Window &operator=(const X11Window &) = delete;

    /// The X window, as LV2's ui:parent and CLAP's x11 window carry it.
    [[nodiscard]] unsigned long id() const noexcept
    {
        return m_window;
    }

    /// Makes it \a width by \a height: what the plugin asked for.
    void resize(int width, int height);
    /// Whether a person may resize it.
    void setResizable(bool resizable);
    /// Puts it on the screen.
    void show();
    /// The size it is now.
    [[nodiscard]] std::pair<int, int> size() const;
    /// Reads what the window has been sent: a close, a new size.
    void pump();

    /// Set from Xlib's handler when the display has gone.
    struct Lost {
        bool lost = false;
    };

private:
    X11Window() = default;

    struct Private;
    std::unique_ptr<Private> d;
    unsigned long m_window = 0;
};

} // namespace rb::pluginhost
