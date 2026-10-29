// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "plugin/messages.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb::pluginhost
{

/// **The host's main loop, as a plugin's window needs it**: timers and file
/// descriptors that call back on the main thread. The host runs them on its
/// PipeWire main loop, which is where everything else of the plugin's main
/// thread runs; a test runs them on one of its own.
class MainLoop
{
public:
    using Id = std::uint32_t;

    /// What a descriptor is watched for, and what it was found to be.
    enum Events : std::uint32_t {
        Readable = 1U << 0,
        Writable = 1U << 1,
        Failed = 1U << 2,
    };

    virtual ~MainLoop() = default;

    /// Calls \a tick every \a period, from the next period on.
    [[nodiscard]] virtual std::optional<Id> addTimer(std::chrono::milliseconds period, std::function<void()> tick) = 0;
    /// Calls \a ready when \a fd is any of \a events.
    [[nodiscard]] virtual std::optional<Id>
    addFd(int fd, std::uint32_t events, std::function<void(std::uint32_t)> ready) = 0;
    [[nodiscard]] virtual bool changeFd(Id id, std::uint32_t events) = 0;
    /// Stops a timer or a watch. Safe from inside its own call.
    virtual void remove(Id id) = 0;
};

/// **What a plugin's window and its presets tell the host**, which tells the
/// engine. Called on the host's main thread.
struct EditorEvents {
    /// A person moved a control in the window: parameter \a index, as the
    /// block numbers them, is \a value as the plugin has it.
    std::function<void(std::uint32_t index, float value)> changed;
    /// A person took hold of the control, or let go of it.
    std::function<void(std::uint32_t index, bool begin)> gesture;
    /// The plugin changed many values itself: \a edit when a person did it
    /// in its window. The host reads them all and tells the engine.
    std::function<void(bool edit)> valuesChanged;
    /// The plugin's own preset these settings are now, by the id presets()
    /// gives it, or empty.
    std::function<void(const std::string &id)> presetShown;
    /// The window has gone by itself: \a why is "closed" when a person
    /// closed it, "failed" when its connection to the display was lost.
    std::function<void(const char *why)> closed;
    /// A value the window set, for the audio thread, as the block's
    /// parameters take it (a choice as its position). For a format whose
    /// window does not change the plugin's values itself.
    std::function<void(std::uint32_t index, float value)> toAudio;
    /// Runs \a change with the audio thread kept away from the plugin, for
    /// a change it cannot take while it runs.
    std::function<void(const std::function<void()> &change)> whileHeld;
};

/// **What a plugin offers besides its sound**: its own window and its own
/// presets. The LV2 and the CLAP plugin each are one; a LADSPA plugin and
/// the built-in ones have neither.
class Editable
{
public:
    enum class Opened : std::uint8_t {
        Open,
        /// It has no window this host can show, or there is no display.
        Unavailable,
        /// The plugin would not make it.
        Failed,
    };

    virtual ~Editable() = default;

    /// Where the window's timers run and whom it tells. Once, before
    /// anything else here; both outlive the plugin.
    void attach(MainLoop &loop, const EditorEvents &events)
    {
        m_loop = &loop;
        m_events = &events;
    }

    /// Shows the plugin's window.
    [[nodiscard]] virtual Opened openUi() = 0;
    /// Closes it; nothing is said through closed().
    virtual void closeUi() = 0;
    [[nodiscard]] virtual bool uiOpen() const = 0;
    /// Whether the plugin has a window this host could show, without
    /// showing it.
    [[nodiscard]] virtual bool hasUi() const = 0;

    /// The plugin's own presets, and putting one in: its values come back
    /// through valuesChanged(true), and presetShown() names it.
    [[nodiscard]] virtual std::vector<plugin::PresetEntry> presets() = 0;
    [[nodiscard]] virtual bool loadPreset(const std::string &id) = 0;
    /// Keeps the settings as a preset of the plugin's own format, where its
    /// other hosts find them; its id, or nothing when this format keeps
    /// them elsewhere or it could not be written.
    [[nodiscard]] virtual std::optional<std::string> savePreset(const std::string &name)
    {
        (void)name;
        return std::nullopt;
    }

protected:
    [[nodiscard]] MainLoop *loop() const noexcept
    {
        return m_loop;
    }
    [[nodiscard]] const EditorEvents *events() const noexcept
    {
        return m_events;
    }

private:
    MainLoop *m_loop = nullptr;
    const EditorEvents *m_events = nullptr;
};

} // namespace rb::pluginhost
