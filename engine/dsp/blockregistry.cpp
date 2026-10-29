// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/blockregistry.h"

#include <utility>

namespace rb::dsp
{

std::expected<void, BlockError> BlockRegistry::add(std::string typeId, Factory factory)
{
    if (typeId.empty() || factory == nullptr) {
        return std::unexpected(BlockError::UnknownType);
    }
    const auto [position, inserted] = m_factories.emplace(std::move(typeId), factory);
    if (!inserted) {
        return std::unexpected(BlockError::DuplicateType);
    }
    return {};
}

std::expected<std::unique_ptr<Block>, BlockError> BlockRegistry::create(std::string_view typeId) const
{
    const auto found = m_factories.find(typeId);
    if (found == m_factories.end()) {
        return std::unexpected(BlockError::UnknownType);
    }
    std::unique_ptr<Block> block = found->second();
    if (block == nullptr) {
        return std::unexpected(BlockError::UnknownType);
    }
    return block;
}

std::expected<BlockInfo, BlockError> BlockRegistry::describe(std::string_view typeId) const
{
    const std::expected<std::unique_ptr<Block>, BlockError> block = create(typeId);
    if (!block) {
        return std::unexpected(block.error());
    }
    return (*block)->info();
}

std::vector<std::string> BlockRegistry::typeIds() const
{
    std::vector<std::string> ids;
    ids.reserve(m_factories.size());
    for (const auto &[typeId, factory] : m_factories) {
        ids.push_back(typeId);
    }
    return ids;
}

bool BlockRegistry::contains(std::string_view typeId) const
{
    return m_factories.contains(typeId);
}

} // namespace rb::dsp
