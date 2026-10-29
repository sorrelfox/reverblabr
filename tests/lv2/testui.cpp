// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The tests' LV2 plugin's window, which never draws anything: a person in a
// box. Shown, it takes hold of the gain and moves it to a quarter, and sends
// the plugin the number 42; then, its hand still on the gain for longer than
// a pause that would end a guessed gesture, it moves it to 0.3 and lets go.
// It writes down every line of what it is told, to the file
// RB_TEST_LV2_UI_LOG names:
//
//   shown | hidden | gone          what the host did with it
//   port <index> <value>          a control, as a port event
//   atom <number>                 an atom:Int the plugin sent back
//
// It closes itself, as a person closing its own window would, once the file
// <log>.close is there. With RB_TEST_LV2_UI_BURST=<n> it also sends n
// numbers at once, from 1000 up.

#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace
{

constexpr std::uint32_t kGain = 0;
constexpr std::uint32_t kControl = 5;

struct Window {
    LV2UI_Write_Function write = nullptr;
    LV2UI_Controller controller = nullptr;
    const LV2UI_Touch *touch = nullptr;
    LV2_URID atomInt = 0;
    LV2_URID eventTransfer = 0;
    std::string log;
    int idles = 0;
};

/// Idle calls between the two moves: about two-thirds of a second at the
/// host's thirty a second, more than the half second a guessed gesture
/// ends after.
constexpr int kHeldFor = 20;

void note(const Window &window, const std::string &line)
{
    if (window.log.empty()) {
        return;
    }
    if (std::FILE *file = std::fopen(window.log.c_str(), "a"); file != nullptr) {
        std::fprintf(file, "%s\n", line.c_str());
        std::fclose(file);
    }
}

LV2UI_Handle instantiate(const LV2UI_Descriptor *,
                         const char *,
                         const char *,
                         LV2UI_Write_Function write,
                         LV2UI_Controller controller,
                         LV2UI_Widget *widget,
                         const LV2_Feature *const *features)
{
    const LV2_URID_Map *map = nullptr;
    const LV2UI_Touch *touch = nullptr;
    for (const LV2_Feature *const *feature = features; feature != nullptr && *feature != nullptr; ++feature) {
        if (std::strcmp((*feature)->URI, LV2_URID__map) == 0) {
            map = static_cast<const LV2_URID_Map *>((*feature)->data);
        } else if (std::strcmp((*feature)->URI, LV2_UI__touch) == 0) {
            touch = static_cast<const LV2UI_Touch *>((*feature)->data);
        }
    }
    if (map == nullptr) {
        return nullptr;
    }
    auto *window = new (std::nothrow) Window;
    if (window == nullptr) {
        return nullptr;
    }
    window->write = write;
    window->controller = controller;
    window->touch = touch;
    window->atomInt = map->map(map->handle, LV2_ATOM__Int);
    window->eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
    if (const char *log = std::getenv("RB_TEST_LV2_UI_LOG"); log != nullptr) {
        window->log = log;
    }
    *widget = nullptr;
    return window;
}

void cleanup(LV2UI_Handle handle)
{
    auto *window = static_cast<Window *>(handle);
    note(*window, "gone");
    delete window;
}

void portEvent(LV2UI_Handle handle, std::uint32_t port, std::uint32_t size, std::uint32_t protocol, const void *buffer)
{
    auto *window = static_cast<Window *>(handle);
    if (protocol == 0 && size == sizeof(float)) {
        char line[64];
        std::snprintf(line, sizeof(line), "port %u %g", port, static_cast<double>(*static_cast<const float *>(buffer)));
        note(*window, line);
    } else if (protocol == window->eventTransfer) {
        const auto *atom = static_cast<const LV2_Atom *>(buffer);
        if (atom->type == window->atomInt) {
            note(*window, "atom " + std::to_string(reinterpret_cast<const LV2_Atom_Int *>(atom)->body));
        }
    }
}

int show(LV2UI_Handle handle)
{
    auto *window = static_cast<Window *>(handle);
    note(*window, "shown");
    return 0;
}

int hide(LV2UI_Handle handle)
{
    note(*static_cast<Window *>(handle), "hidden");
    return 0;
}

int idle(LV2UI_Handle handle)
{
    auto *window = static_cast<Window *>(handle);
    const int idles = window->idles++;
    if (idles == 0) {
        // **A person's drag**: hold and move; and a word for the plugin
        // through its atom port.
        if (window->touch != nullptr) {
            window->touch->touch(window->touch->handle, kGain, true);
        }
        const float quarter = 0.25f;
        window->write(window->controller, kGain, sizeof(float), 0, &quarter);
        const LV2_Atom_Int number{{sizeof(std::int32_t), window->atomInt}, 42};
        window->write(window->controller, kControl, sizeof(number), window->eventTransfer, &number);
        // **A burst**, more than one cycle's sequence holds, when asked
        // for: 1000, 1001 and on.
        if (const char *burst = std::getenv("RB_TEST_LV2_UI_BURST"); burst != nullptr) {
            const int count = std::atoi(burst);
            for (int at = 0; at < count; ++at) {
                const LV2_Atom_Int more{{sizeof(std::int32_t), window->atomInt}, 1000 + at};
                window->write(window->controller, kControl, sizeof(more), window->eventTransfer, &more);
            }
        }
    } else if (idles == kHeldFor) {
        // Moved again, after a rest, and let go.
        const float more = 0.3f;
        window->write(window->controller, kGain, sizeof(float), 0, &more);
        if (window->touch != nullptr) {
            window->touch->touch(window->touch->handle, kGain, false);
        }
    }
    return !window->log.empty() && ::access((window->log + ".close").c_str(), F_OK) == 0 ? 1 : 0;
}

const void *extensionData(const char *uri)
{
    static const LV2UI_Show_Interface shows{show, hide};
    static const LV2UI_Idle_Interface idles{idle};
    if (std::strcmp(uri, LV2_UI__showInterface) == 0) {
        return &shows;
    }
    if (std::strcmp(uri, LV2_UI__idleInterface) == 0) {
        return &idles;
    }
    return nullptr;
}

const LV2UI_Descriptor g_descriptor{"urn:reverblabr:test:gain#window", instantiate, cleanup, portEvent, extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2UI_Descriptor *lv2ui_descriptor(std::uint32_t index)
{
    return index == 0 ? &g_descriptor : nullptr;
}
