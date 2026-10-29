// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/blockinfo.h"

#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// What block types exist, and how to make one.
///
/// A session file names a block by its stable type id; this is what turns
/// that id into a block. It is also what the library sidebar lists, so a
/// block type has to be registered exactly once, at start-up, before a
/// session is opened.
///
/// **The registry holds stable ids and nothing else.** There are no names and
/// no descriptions here: those are translated and belong to the interface,
/// which keys its own table by the type id (see docs/i18n.md).
///
/// Nothing here is real-time. Registering and creating happen off the audio
/// thread, before a graph goes live.
class BlockRegistry
{
public:
    /// Makes one block. A captureless function, so registering costs nothing.
    using Factory = std::unique_ptr<Block> (*)();

    BlockRegistry() = default;

    /// Registers \a factory under \a typeId. The same id twice is a mistake
    /// in our own code and is reported rather than ignored, because the
    /// second registration would silently shadow the first.
    [[nodiscard]] std::expected<void, BlockError> add(std::string typeId, Factory factory);

    /// Makes a block of that type, or says the type is unknown — which is
    /// what a session written by a later version leads to.
    [[nodiscard]] std::expected<std::unique_ptr<Block>, BlockError> create(std::string_view typeId) const;

    /// What the block of that type is, without keeping one. Makes a block,
    /// copies its description and throws the block away; a block costs
    /// nothing until it is prepared.
    [[nodiscard]] std::expected<BlockInfo, BlockError> describe(std::string_view typeId) const;

    /// Every registered type id, in id order, so that a listing does not
    /// depend on the order things were registered in.
    [[nodiscard]] std::vector<std::string> typeIds() const;

    [[nodiscard]] bool contains(std::string_view typeId) const;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_factories.size();
    }

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return m_factories.empty();
    }

private:
    std::map<std::string, Factory, std::less<>> m_factories;
};

} // namespace rb::dsp
