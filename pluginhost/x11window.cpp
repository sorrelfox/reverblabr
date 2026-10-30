// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "x11window.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace rb::pluginhost
{

namespace
{

/// What a window that has never been shown starts as, when its plugin does
/// not say.
constexpr int kFirstWidth = 640;
constexpr int kFirstHeight = 480;

/// The places file, read whole: one "<key>\t<x> <y> <width> <height>" a line.
[[nodiscard]] std::map<std::string, WindowPlace> readPlaces()
{
    std::map<std::string, WindowPlace> places;
    std::ifstream in(placesFile());
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        std::istringstream numbers(line.substr(tab + 1));
        WindowPlace place;
        if (numbers >> place.x >> place.y >> place.width >> place.height && place.width > 0 && place.height > 0) {
            places[line.substr(0, tab)] = place;
        }
    }
    return places;
}

} // namespace

std::string placesFile()
{
    std::filesystem::path folder;
    if (const char *state = std::getenv("XDG_STATE_HOME"); state != nullptr && *state == '/') {
        folder = state;
    } else if (const char *home = std::getenv("HOME"); home != nullptr) {
        folder = std::filesystem::path{home} / ".local" / "state";
    } else {
        return {};
    }
    return (folder / "reverblabr" / "plugin-windows").string();
}

std::optional<WindowPlace> placeOf(const std::string &key)
{
    const std::map<std::string, WindowPlace> places = readPlaces();
    const auto found = places.find(key);
    return found == places.end() ? std::nullopt : std::optional{found->second};
}

void rememberPlace(const std::string &key, const WindowPlace &place)
{
    const std::string file = placesFile();
    if (file.empty() || key.find_first_of("\t\n") != std::string::npos) {
        return;
    }
    std::map<std::string, WindowPlace> places = readPlaces();
    places[key] = place;
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path{file}.parent_path(), ignored);
    // **Written aside and moved over**: two hosts closing windows at once
    // each leave a whole file, one of them the last.
    const std::string written = file + ".new" + std::to_string(::getpid());
    {
        std::ofstream out(written, std::ios::trunc);
        for (const auto &[name, at] : places) {
            out << name << '\t' << at.x << ' ' << at.y << ' ' << at.width << ' ' << at.height << '\n';
        }
    }
    std::filesystem::rename(written, file, ignored);
}

namespace
{

/// **An X error is said, not fatal**: Xlib's own handler ends the process,
/// which would take the plugin's sound down with its window. A request
/// the window made that failed is no reason for that.
int onXError(Display *display, XErrorEvent *error)
{
    std::array<char, 256> text{};
    XGetErrorText(display, error->error_code, text.data(), static_cast<int>(text.size()));
    std::fprintf(stderr, "a plugin window's X request failed: %s\n", text.data());
    return 0;
}

/// **The display gone** (XWayland restarted): Xlib ends the process
/// after this unless it returns, which it does; the window is taken down
/// on the loop's next turn and the engine told it failed.
void onDisplayLost(Display *, void *data)
{
    static_cast<X11Window::Lost *>(data)->lost = true;
}

} // namespace

bool haveDisplay()
{
    const char *display = std::getenv("DISPLAY");
    return display != nullptr && *display != '\0';
}

struct X11Window::Private {
    MainLoop *loop = nullptr;
    Display *display = nullptr;
    Atom deleteWindow = 0;
    std::optional<MainLoop::Id> watch;
    std::string key;
    std::function<void(bool)> closed;
    /// The connection to the display is lost: nothing more is asked of it.
    Lost lost;
    std::function<void(int, int)> resized;
    int width = 0;
    int height = 0;
    bool closing = false;
};

std::unique_ptr<X11Window> X11Window::open(MainLoop &loop,
                                           const std::string &title,
                                           const std::string &key,
                                           int width,
                                           int height,
                                           std::function<void(bool)> closed,
                                           std::function<void(int, int)> resized)
{
    if (!haveDisplay()) {
        return nullptr;
    }
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return nullptr;
    }
    std::unique_ptr<X11Window> window(new X11Window());
    window->d = std::make_unique<Private>();
    Private &d = *window->d;
    XSetErrorHandler(onXError);
    XSetIOErrorExitHandler(display, onDisplayLost, &d.lost);
    d.loop = &loop;
    d.display = display;
    d.key = key;
    d.closed = std::move(closed);
    d.resized = std::move(resized);

    const std::optional<WindowPlace> kept = placeOf(key);
    d.width = kept ? kept->width : (width > 0 ? width : kFirstWidth);
    d.height = kept ? kept->height : (height > 0 ? height : kFirstHeight);
    const int screen = DefaultScreen(display);
    XSetWindowAttributes attributes{};
    attributes.event_mask = StructureNotifyMask;
    attributes.background_pixel = BlackPixel(display, screen);
    window->m_window = XCreateWindow(display,
                                     RootWindow(display, screen),
                                     kept ? kept->x : 0,
                                     kept ? kept->y : 0,
                                     static_cast<unsigned>(d.width),
                                     static_cast<unsigned>(d.height),
                                     0,
                                     CopyFromParent,
                                     InputOutput,
                                     // The parent's visual: CopyFromParent is a
                                     // plain 0 in older libX11, not a pointer.
                                     static_cast<Visual *>(nullptr),
                                     CWEventMask | CWBackPixel,
                                     &attributes);
    XStoreName(display, window->m_window, title.c_str());
    // The title as UTF-8 too, which is what a desktop reads.
    const Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display,
                    window->m_window,
                    XInternAtom(display, "_NET_WM_NAME", False),
                    utf8,
                    8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char *>(title.data()),
                    static_cast<int>(title.size()));
    // Which program it is, for the desktop's grouping and its icon.
    XClassHint hint{};
    std::string name = "reverblabr-plugin-host";
    std::string className = "Reverblabr";
    hint.res_name = name.data();
    hint.res_class = className.data();
    XSetClassHint(display, window->m_window, &hint);
    if (kept) {
        // **Where it was**, which a window manager takes from the hints
        // rather than from where the window was made.
        XSizeHints hints{};
        hints.flags = USPosition | USSize;
        hints.x = kept->x;
        hints.y = kept->y;
        hints.width = kept->width;
        hints.height = kept->height;
        XSetWMNormalHints(display, window->m_window, &hints);
    }
    d.deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window->m_window, &d.deleteWindow, 1);

    X11Window *self = window.get();
    d.watch = loop.addFd(ConnectionNumber(display), MainLoop::Readable, [self](std::uint32_t) { self->pump(); });
    XFlush(display);
    return window;
}

X11Window::~X11Window()
{
    if (!d) {
        return;
    }
    if (d->watch) {
        d->loop->remove(*d->watch);
    }
    if (d->lost.lost) {
        return; // a display that has gone is asked nothing more
    }
    // **Where it is now**, on the screen rather than in its frame: the
    // window manager put a frame around it, and the window's own position
    // is inside that.
    Window child = 0;
    int x = 0;
    int y = 0;
    if (XTranslateCoordinates(d->display, m_window, DefaultRootWindow(d->display), 0, 0, &x, &y, &child) != 0
        && d->width > 0 && d->height > 0) {
        rememberPlace(d->key, WindowPlace{x, y, d->width, d->height});
    }
    XDestroyWindow(d->display, m_window);
    XCloseDisplay(d->display);
}

void X11Window::resize(int width, int height)
{
    if (d->lost.lost) {
        return;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    d->width = width;
    d->height = height;
    XResizeWindow(d->display, m_window, static_cast<unsigned>(width), static_cast<unsigned>(height));
    XFlush(d->display);
}

void X11Window::setResizable(bool resizable)
{
    if (d->lost.lost) {
        return;
    }
    XSizeHints hints{};
    long supplied = 0;
    XGetWMNormalHints(d->display, m_window, &hints, &supplied);
    if (resizable) {
        hints.flags &= ~(PMinSize | PMaxSize);
    } else {
        hints.flags |= PMinSize | PMaxSize;
        hints.min_width = hints.max_width = d->width;
        hints.min_height = hints.max_height = d->height;
    }
    XSetWMNormalHints(d->display, m_window, &hints);
    XFlush(d->display);
}

void X11Window::show()
{
    if (d->lost.lost) {
        return;
    }
    XMapRaised(d->display, m_window);
    XFlush(d->display);
}

std::pair<int, int> X11Window::size() const
{
    return {d->width, d->height};
}

void X11Window::pump()
{
    const auto lost = [this] {
        if (!d->lost.lost) {
            return false;
        }
        if (!std::exchange(d->closing, true) && d->closed) {
            const std::function<void(bool)> closed = d->closed;
            closed(true);
        }
        return true;
    };
    while (!lost() && XPending(d->display) > 0) {
        if (lost()) {
            return;
        }
        XEvent event{};
        XNextEvent(d->display, &event);
        if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == d->deleteWindow) {
            // **A person closed it.** Said once; the host takes the window
            // down, which may be from inside this call, so what is called
            // is a copy, and nothing of the window is touched after it.
            if (!std::exchange(d->closing, true) && d->closed) {
                const std::function<void(bool)> closed = d->closed;
                closed(false);
                return;
            }
        } else if (event.type == ConfigureNotify && event.xconfigure.window == m_window) {
            const bool changed = event.xconfigure.width != d->width || event.xconfigure.height != d->height;
            d->width = event.xconfigure.width;
            d->height = event.xconfigure.height;
            if (changed && d->resized) {
                d->resized(d->width, d->height);
            }
        }
    }
}

} // namespace rb::pluginhost
