// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramdescriptor.h"
#include "dsp/paramvalue.h"
#include "rt/realtime.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// Holds the values of one block's parameters, so that a block does not have
/// to write the same lookup, range check and save-and-load code again.
///
/// The store keeps two kinds of value apart, because the audio thread can
/// only touch one of them:
///
/// - **Numbers** — a float, an int, a bool, or which choice of an enum is
///   taken. These live in an array of floats that was sized in the
///   constructor, so setting one on the audio thread writes a float and
///   nothing else. That is what applyNumber() does.
/// - **Text** — a string or a device reference. These own memory, so they are
///   never set from the audio thread. Changing one changes the graph and goes
///   through a recompile.
///
/// ParamValue itself never reaches the audio thread. What applyNumber() needs
/// of a descriptor — the type, the range, how many choices there are — is
/// worked out once in the constructor and kept as plain numbers.
///
/// An enum is stored as the position of its choice and saved as the choice's
/// **stable id**, so that reordering the choices in a later version does not
/// change what an old session file means.
class ParamStore
{
public:
    /// Takes the block's descriptors, which must outlive the store, and sets
    /// every parameter to its default. Allocates; belongs in the block's
    /// constructor, not in a cycle.
    explicit ParamStore(std::span<const ParamDescriptor> descriptors);

    [[nodiscard]] std::span<const ParamDescriptor> descriptors() const noexcept
    {
        return m_descriptors;
    }

    [[nodiscard]] std::size_t size() const noexcept RB_NONBLOCKING
    {
        return m_numbers.size();
    }

    /// Where a parameter sits in the list, or nothing if there is no such id.
    [[nodiscard]] std::optional<std::uint32_t> indexOf(std::string_view id) const;

    /// Sets every parameter back to its default.
    void resetToDefaults();

    // --- the audio thread's side ---

    /// The number behind parameter \a index: the value itself for a float,
    /// the whole number for an int, 0 or 1 for a bool, the position of the
    /// choice for an enum, and 0 for a text parameter.
    [[nodiscard]] float number(std::uint32_t index) const noexcept RB_NONBLOCKING
    {
        return index < m_numbers.size() ? m_numbers[index].load(std::memory_order_relaxed) : 0.0f;
    }

    /// Sets that number, clamped to the parameter's range. Safe on the audio
    /// thread; it writes one float. A text parameter is left alone.
    ///
    /// **Every number is a relaxed atomic**. The audio thread writes
    /// them here and reads them in a cycle, while the engine thread sets them
    /// from an edit and reads them to save — autosave does, while the
    /// session runs. Each value is read whole; no order between two of them
    /// is promised, and none is needed: a save taken during a drag holds some
    /// moment of the drag for each setting.
    void applyNumber(std::uint32_t index, float number) noexcept RB_NONBLOCKING;

    // --- everything else's side ---

    /// The value of a parameter, built from what is stored. An empty value
    /// means there is no such parameter.
    [[nodiscard]] ParamValue value(std::uint32_t index) const;
    [[nodiscard]] ParamValue value(std::string_view id) const;

    /// Sets a parameter, checking the type and the range.
    [[nodiscard]] std::expected<void, BlockError> set(std::uint32_t index, const ParamValue &value);
    [[nodiscard]] std::expected<void, BlockError> set(std::string_view id, const ParamValue &value);

    /// Sets a read-only parameter, which is how a block publishes an output
    /// value such as a gain reduction. Only the block itself may call this.
    [[nodiscard]] std::expected<void, BlockError> setReadOnly(std::uint32_t index, const ParamValue &value);

    /// Every parameter that is not read-only, by id. Read-only parameters are
    /// block outputs; they are telemetry, not state.
    [[nodiscard]] ParamValueMap save() const;

    /// Puts a saved state back. A parameter the state does not mention is set
    /// to its default, so loading twice gives the same result. An id the
    /// block does not know is ignored, so that a session written by a later
    /// version still opens; a known id with a bad value is an error.
    [[nodiscard]] std::expected<void, BlockError> load(const ParamValueMap &state);

private:
    /// What applyNumber() needs to know about a parameter, in a form the
    /// audio thread can read without touching anything that owns memory.
    struct NumberLimits {
        ParamType type = ParamType::Float;
        bool bounded = false;
        float minimum = 0.0f;
        float maximum = 0.0f;
        std::uint32_t choiceCount = 0;
    };

    [[nodiscard]] static float numberOf(const ParamDescriptor &descriptor, const ParamValue &value);

    std::span<const ParamDescriptor> m_descriptors;
    std::vector<NumberLimits> m_limits;
    /// One per parameter, whatever its type; a text parameter's entry is 0.
    /// Atomic, because two threads share them (see applyNumber()).
    std::vector<std::atomic<float>> m_numbers;
    static_assert(std::atomic<float>::is_always_lock_free,
                  "a number is shared with the audio thread, which may not lock");
    /// One per parameter; only the text types use theirs.
    std::vector<std::string> m_texts;
    /// One per parameter; only a list parameter uses its own.
    std::vector<std::vector<std::string>> m_lists;
};

} // namespace rb::dsp
