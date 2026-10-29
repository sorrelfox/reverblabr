// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The tests' LV2 plugins: a stereo gain that says on its control output what
// it is set to, and answers every atom:Int its window sends with the next
// number up, through its notify port. What it hears and says is what the
// host's atom ports are tested with. The second is the same gain doubled,
// with the same settings: another plugin whose settings share ids.

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>

#include <cstdint>
#include <cstring>
#include <new>

namespace
{

enum Port : std::uint32_t {
    Gain = 0,
    InLeft = 1,
    InRight = 2,
    OutLeft = 3,
    OutRight = 4,
    Control = 5,
    Notify = 6,
    Level = 7,
};

struct Plugin {
    /// 2 for the doubled one.
    float scale = 1.0f;
    const float *gain = nullptr;
    const float *in[2] = {nullptr, nullptr};
    float *out[2] = {nullptr, nullptr};
    const LV2_Atom_Sequence *control = nullptr;
    LV2_Atom_Sequence *notify = nullptr;
    float *level = nullptr;
    LV2_URID atomInt = 0;
    LV2_Atom_Forge forge{};
};

LV2_Handle instantiate(const LV2_Descriptor *descriptor, double, const char *, const LV2_Feature *const *features)
{
    const LV2_URID_Map *map = nullptr;
    for (const LV2_Feature *const *feature = features; feature != nullptr && *feature != nullptr; ++feature) {
        if (std::strcmp((*feature)->URI, LV2_URID__map) == 0) {
            map = static_cast<const LV2_URID_Map *>((*feature)->data);
        }
    }
    if (map == nullptr) {
        return nullptr;
    }
    auto *plugin = new (std::nothrow) Plugin;
    if (plugin == nullptr) {
        return nullptr;
    }
    plugin->atomInt = map->map(map->handle, LV2_ATOM__Int);
    plugin->scale = std::strcmp(descriptor->URI, "urn:reverblabr:test:double") == 0 ? 2.0f : 1.0f;
    lv2_atom_forge_init(&plugin->forge, const_cast<LV2_URID_Map *>(map));
    return plugin;
}

void connect(LV2_Handle handle, std::uint32_t port, void *data)
{
    auto *plugin = static_cast<Plugin *>(handle);
    switch (port) {
    case Gain:
        plugin->gain = static_cast<const float *>(data);
        break;
    case InLeft:
    case InRight:
        plugin->in[port - InLeft] = static_cast<const float *>(data);
        break;
    case OutLeft:
    case OutRight:
        plugin->out[port - OutLeft] = static_cast<float *>(data);
        break;
    case Control:
        plugin->control = static_cast<const LV2_Atom_Sequence *>(data);
        break;
    case Notify:
        plugin->notify = static_cast<LV2_Atom_Sequence *>(data);
        break;
    case Level:
        plugin->level = static_cast<float *>(data);
        break;
    default:
        break;
    }
}

void run(LV2_Handle handle, std::uint32_t frames)
{
    auto *plugin = static_cast<Plugin *>(handle);
    const float gain = (plugin->gain != nullptr ? *plugin->gain : 1.0f) * plugin->scale;
    for (int side = 0; side < 2; ++side) {
        if (plugin->in[side] == nullptr || plugin->out[side] == nullptr) {
            continue;
        }
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            plugin->out[side][frame] = plugin->in[side][frame] * gain;
        }
    }
    if (plugin->level != nullptr) {
        *plugin->level = plugin->gain != nullptr ? *plugin->gain : 1.0f;
    }
    if (plugin->notify == nullptr) {
        return;
    }
    // The host says how much room there is in the atom's size.
    const std::uint32_t room = plugin->notify->atom.size;
    lv2_atom_forge_set_buffer(&plugin->forge, reinterpret_cast<std::uint8_t *>(plugin->notify), room);
    LV2_Atom_Forge_Frame frame{};
    lv2_atom_forge_sequence_head(&plugin->forge, &frame, 0);
    if (plugin->control != nullptr) {
        LV2_ATOM_SEQUENCE_FOREACH(plugin->control, event)
        {
            if (event->body.type == plugin->atomInt) {
                const auto *said = reinterpret_cast<const LV2_Atom_Int *>(&event->body);
                lv2_atom_forge_frame_time(&plugin->forge, 0);
                lv2_atom_forge_int(&plugin->forge, said->body + 1);
            }
        }
    }
    lv2_atom_forge_pop(&plugin->forge, &frame);
}

void cleanup(LV2_Handle handle)
{
    delete static_cast<Plugin *>(handle);
}

const LV2_Descriptor
    g_descriptor{"urn:reverblabr:test:gain", instantiate, connect, nullptr, run, nullptr, cleanup, nullptr};
const LV2_Descriptor
    g_double{"urn:reverblabr:test:double", instantiate, connect, nullptr, run, nullptr, cleanup, nullptr};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(std::uint32_t index)
{
    return index == 0 ? &g_descriptor : index == 1 ? &g_double : nullptr;
}
