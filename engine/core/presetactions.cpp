// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/presetactions.h"

#include "core/paramjson.h"
#include "core/session.h"
#include "dsp/block.h"
#include "dsp/blockregistry.h"
#include "dsp/paramdescriptor.h"

#include <QJsonObject>

#include <algorithm>
#include <memory>
#include <span>

using namespace Qt::StringLiterals;

namespace rb::core
{

std::expected<ApplyPreset, PresetError> applyPresetEdit(const dsp::BlockRegistry &registry,
                                                        const Session &session,
                                                        const QString &blockId,
                                                        const Preset &preset)
{
    const BlockInstance *block = session.findBlock(blockId);
    if (block == nullptr) {
        return std::unexpected(PresetError::NotFound);
    }
    // **A preset belongs to a kind of block.** Putting a Dynamics preset on
    // a Volume block would set parameters it does not have and leave the
    // ones it does wherever they were.
    if (block->type != preset.typeId) {
        return std::unexpected(PresetError::WrongType);
    }
    if (preset.params.isEmpty()) {
        return std::unexpected(PresetError::NotAPreset);
    }

    ApplyPreset edit;
    edit.blockId = blockId;
    edit.presetId = preset.id;
    edit.params = preset.params;
    // Nothing to clear: see the header. What it does not mention, it does
    // not touch — and what is not a preset's to set, it does not set.
    if (const std::expected<std::unique_ptr<dsp::Block>, dsp::BlockError> made =
            registry.create(block->type.toStdString());
        made) {
        for (const dsp::ParamDescriptor &descriptor : (*made)->params()) {
            if (!descriptor.flags.inPresets) {
                edit.params.remove(QString::fromStdString(descriptor.id));
            }
        }
    }
    if (edit.params.isEmpty()) {
        return std::unexpected(PresetError::NotAPreset);
    }
    return edit;
}

std::expected<Preset, PresetError>
presetOfBlock(const dsp::BlockRegistry &registry, const Session &session, const QString &blockId, const QString &name)
{
    const BlockInstance *block = session.findBlock(blockId);
    if (block == nullptr) {
        return std::unexpected(PresetError::NotFound);
    }
    if (!isValidPresetName(name)) {
        return std::unexpected(PresetError::BadName);
    }
    const std::expected<std::unique_ptr<dsp::Block>, dsp::BlockError> made = registry.create(block->type.toStdString());
    if (!made) {
        // A block type this build does not have: its parameters are whatever
        // the file held, and this build has no idea which of them matter.
        return std::unexpected(PresetError::WrongType);
    }

    // **A block whose settings depend on which plugin it runs** is made
    // with that plugin, so its settings are the plugin's and the preset
    // holds every one of them, set or not.
    if (const QJsonValue plugin = block->params.value("plugin"_L1); plugin.isString()) {
        const std::span<const dsp::ParamDescriptor> own = (*made)->params();
        if (std::ranges::any_of(own, [](const dsp::ParamDescriptor &d) { return d.id == "plugin"; })) {
            (void)(*made)->setParam("plugin", dsp::ParamValue::ofString(plugin.toString().toStdString()));
        }
    }

    Preset preset;
    preset.typeId = block->type;
    preset.name = name.trimmed();
    preset.id = preset.name;
    for (const dsp::ParamDescriptor &descriptor : (*made)->params()) {
        // **Read-only parameters are not saved.** A device's channel count
        // or a meter's reading is something the block says about itself, not
        // a setting somebody chose, and putting it in a preset would be
        // telling another block a fact about this one.
        if (descriptor.flags.readOnly) {
            continue;
        }
        // **Nor what says which of a pair this block is**: a preset
        // saved from the left-ear block has to load into the right one
        // without turning it into a second left.
        if (!descriptor.flags.inPresets) {
            continue;
        }
        const QString id = QString::fromStdString(descriptor.id);
        const QJsonValue set = block->params.value(id);
        preset.params.insert(id, set.isUndefined() ? valueToJson(descriptor.defaultValue) : set);
    }
    if (preset.params.isEmpty()) {
        return std::unexpected(PresetError::NotAPreset);
    }
    return preset;
}

QJsonArray presetsToJson(const QList<Preset> &presets)
{
    QJsonArray array;
    for (const Preset &preset : presets) {
        array.append(QJsonObject{
            {"id"_L1, preset.id},
            {"name"_L1, preset.name},
            {"factory"_L1, preset.factory},
            {"params"_L1, preset.params},
        });
    }
    return array;
}

} // namespace rb::core
