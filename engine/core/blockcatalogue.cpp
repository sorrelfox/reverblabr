// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/blockcatalogue.h"

#include "core/paramjson.h"
#include "dsp/block.h"

#include <QJsonArray>

#include <cstdint>
#include <expected>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

[[nodiscard]] QString asText(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] QJsonObject portsJson(const dsp::PortSpec &ports, bool audioSystem)
{
    QJsonArray labels;
    for (const std::string &label : ports.labels) {
        labels.append(QString::fromStdString(label));
    }
    QJsonArray laneLabels;
    for (const std::string &label : ports.laneLabels) {
        laneLabels.append(QString::fromStdString(label));
    }
    return QJsonObject{
        {"minimum"_L1, static_cast<int>(ports.minCount)},
        {"maximum"_L1, static_cast<int>(ports.maxCount)},
        {"labels"_L1, labels},
        // **Lanes**: how many ports make one connector on the canvas,
        // and what each connector is called. A width of nought is one lane
        // however many ports there are.
        {"laneWidth"_L1, static_cast<int>(ports.laneWidth)},
        {"lanes"_L1, laneLabels},
        // **Whether these are ports at all, as far as the canvas goes**.
        // The audio system fills a capture block's inputs and takes
        // an output block's outputs; a wire to either is refused, and the
        // interface reads this rather than guessing from the category.
        {"audioSystem"_L1, audioSystem},
    };
}

[[nodiscard]] QJsonObject descriptorJson(const dsp::ParamDescriptor &descriptor,
                                         const std::map<std::string, std::string> &unavailable)
{
    QJsonObject entry{
        {"id"_L1, QString::fromStdString(descriptor.id)},
        {"type"_L1, asText(dsp::paramTypeName(descriptor.type))},
        {"unit"_L1, asText(dsp::paramUnitName(descriptor.unit))},
        {"taper"_L1, asText(dsp::paramTaperName(descriptor.taper))},
        {"default"_L1, valueToJson(descriptor.defaultValue)},
    };
    // **Only what the parameter really has.** A range on a bool or an enum
    // would be a number the panel has to know to ignore, and a field that is
    // sometimes meaningless is a field that is sometimes read anyway.
    if (!descriptor.minimum.isEmpty()) {
        entry.insert("minimum"_L1, valueToJson(descriptor.minimum));
    }
    if (!descriptor.maximum.isEmpty()) {
        entry.insert("maximum"_L1, valueToJson(descriptor.maximum));
    }
    if (!descriptor.choices.empty()) {
        QJsonArray choices;
        QJsonArray missing;
        for (const std::string &choice : descriptor.choices) {
            const auto refused = unavailable.find(choice);
            if (refused == unavailable.end()) {
                choices.append(QString::fromStdString(choice));
            } else {
                missing.append(QJsonObject{{"id"_L1, QString::fromStdString(choice)},
                                           {"reason"_L1, QString::fromStdString(refused->second)}});
            }
        }
        entry.insert("choices"_L1, choices);
        if (!missing.isEmpty()) {
            entry.insert("unavailableChoices"_L1, missing);
        }
    }
    if (descriptor.smoothingMs > 0.0f) {
        entry.insert("smoothingMs"_L1, static_cast<double>(descriptor.smoothingMs));
    }
    // **A block's outputs, not its settings.** A read-only parameter is
    // something the block says about itself; the panel shows it and does not
    // offer to change it.
    if (descriptor.flags.readOnly) {
        entry.insert("readOnly"_L1, true);
    }
    if (descriptor.flags.hidden) {
        entry.insert("hidden"_L1, true);
    }
    // **What may be dragged.** A parameter that is not automatable is one
    // the engine plans the session again for, so a control that streamed
    // values as it moved would recompile the graph at every tick; the
    // interface sends one value when the choice is made instead.
    if (!descriptor.flags.automatable) {
        entry.insert("automatable"_L1, false);
    }
    // **What a preset leaves alone**, which is also what putting a block
    // back to its defaults leaves alone: which ear a hearing block is for
    // says which of a pair it is, not how it sounds.
    if (!descriptor.flags.inPresets) {
        entry.insert("inPresets"_L1, false);
    }
    return entry;
}

} // namespace

QJsonObject blockCatalogueJson(const dsp::BlockRegistry &registry, const UnavailableChoices &unavailable)
{
    QJsonArray blocks;
    for (const std::string &typeId : registry.typeIds()) {
        // **A block costs nothing until it is prepared**, so the cheapest
        // way to ask what it can do is to make one and throw it away.
        const std::expected<std::unique_ptr<dsp::Block>, dsp::BlockError> made = registry.create(typeId);
        if (!made) {
            continue;
        }
        const dsp::Block &block = **made;
        const dsp::BlockInfo &info = block.info();

        QJsonArray params;
        for (const dsp::ParamDescriptor &descriptor : block.params()) {
            const auto refused = unavailable.find({typeId, descriptor.id});
            params.append(descriptorJson(
                descriptor, refused == unavailable.end() ? std::map<std::string, std::string>{} : refused->second));
        }

        blocks.append(QJsonObject{
            {"type"_L1, QString::fromStdString(typeId)},
            {"category"_L1, asText(dsp::blockCategoryName(info.category))},
            {"face"_L1, asText(dsp::faceKindName(info.face))},
            {"inputs"_L1, portsJson(info.inputs, info.inputsFromAudioSystem)},
            {"outputs"_L1, portsJson(info.outputs, info.outputsToAudioSystem)},
            {"params"_L1, params},
        });
    }
    return QJsonObject{{"blocks"_L1, blocks}};
}

QString wireRefusal(const Session &session, const dsp::BlockRegistry &registry, const Wire &wire)
{
    /// What the registry says about the block \a blockId is, or nothing
    /// when there is no block of that id or this build has never heard of
    /// its type.
    const auto describe = [&](const QString &blockId) -> std::optional<dsp::BlockInfo> {
        const BlockInstance *block = session.findBlock(blockId);
        if (block == nullptr) {
            return std::nullopt; // applyEdit() is the one that says a block is missing
        }
        const std::expected<dsp::BlockInfo, dsp::BlockError> info = registry.describe(block->type.toStdString());
        return info ? std::optional<dsp::BlockInfo>{*info} : std::nullopt;
    };
    const std::optional<dsp::BlockInfo> from = describe(wire.fromBlock);
    const std::optional<dsp::BlockInfo> to = describe(wire.toBlock);

    // **The most the block can have, not the fewest.** A block whose shape
    // follows what is connected to it — a mixer, a device with more than two
    // channels — grows a port when a wire asks for one, which is exactly
    // what the graph compiler does with the same numbers.
    if (from && wire.fromPort >= from->outputs.maxCount) {
        return u"wirePortOutOfRange"_s;
    }
    if (to && wire.toPort >= to->inputs.maxCount) {
        return u"wirePortOutOfRange"_s;
    }
    // **Ports the audio system fills are not wiring**: what arrives
    // at a capture block is the application or device it follows, and an
    // output block's outputs belong to the device.
    if (to && to->inputsFromAudioSystem) {
        return u"wireIntoSource"_s;
    }
    if (from && from->outputsToAudioSystem) {
        return u"wireOutOfOutput"_s;
    }
    return {};
}

} // namespace rb::core
