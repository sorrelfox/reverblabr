// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scan.h"

#include "ladspaplugin.h"
#include "plugin/formats.h"
#include "plugin/testplugins.h"
#ifdef RB_HAVE_CLAP
#include "clapplugin.h"
#endif
#ifdef RB_HAVE_VST3
#include "vst3module.h"
#endif

#include <lilv/lilv.h>
#include <lv2/core/lv2.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace rb::pluginhost
{

namespace
{

using plugin::PluginDescription;

/// **One plugin's line, written the moment it is described**: the
/// engine takes each line as a sign of progress, and gives up on a
/// candidate only when none has come for a while. A file of many plugins
/// that is slow on a busy machine is then never mistaken for one that
/// hangs. Written to \a fd straight, past stdio's buffer.
void emit(const PluginDescription &description, int fd = STDOUT_FILENO)
{
    const std::string line = plugin::toJsonLine(description);
    std::size_t written = 0;
    while (written < line.size()) {
        const ssize_t now = ::write(fd, line.data() + written, line.size() - written);
        if (now <= 0) {
            if (now < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
        written += static_cast<std::size_t>(now);
    }
}

/// The plugins built into the host, and the test candidates: a `.rbtest`
/// file says "crash", "hang" or "unreadable", or lists plugins as
/// `id|name|vendor` lines. Between them, `sleep <ms>` waits (a file slow to
/// describe) and `hang` stops for ever (a file that hangs part of the way
/// through, after the plugins above it are told).
int scanBuiltin(const std::string &path)
{
    if (path == "builtin") {
        for (const auto &[id, name] : {std::pair{plugin::TestGain::kTypeId, "Test gain"},
                                       std::pair{plugin::TestGain::kSlowTypeId, "Test gain, slow to load"}}) {
            const std::unique_ptr<dsp::Block> made = plugin::makeBuiltinPlugin(id, 0);
            PluginDescription description;
            description.format = "builtin";
            description.id = std::string{id};
            description.name = name;
            description.vendor = "Reverblabr";
            description.category = "Utility";
            description.audioInputs = made->info().inputs.maxCount;
            description.audioOutputs = made->info().outputs.maxCount;
            for (const dsp::ParamDescriptor &descriptor : made->params()) {
                plugin::PluginParam param;
                param.id = descriptor.id;
                param.name = descriptor.id;
                param.minimum = static_cast<float>(descriptor.minimum.asNumber().value_or(0.0));
                param.maximum = static_cast<float>(descriptor.maximum.asNumber().value_or(1.0));
                param.defaultValue = static_cast<float>(descriptor.defaultValue.asNumber().value_or(0.0));
                description.params.push_back(param);
            }
            description.parameters = static_cast<std::uint32_t>(description.params.size());
            emit(description);
        }
        return 0;
    }
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    const std::string content = text.str();
    if (content.starts_with("crash")) {
        std::abort(); // what a plugin that crashes on being loaded does
    }
    if (content.starts_with("hang")) {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }
    }
    if (content.starts_with("unreadable")) {
        std::fprintf(stderr, "not a plugin: %s\n", path.c_str());
        return kScanUnreadable;
    }
    std::istringstream lines(content);
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with("sleep ")) {
            std::this_thread::sleep_for(std::chrono::milliseconds{std::atoi(line.c_str() + 6)});
            continue;
        }
        if (line == "hang") {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds{1});
            }
        }
        const std::size_t first = line.find('|');
        const std::size_t second = first == std::string::npos ? first : line.find('|', first + 1);
        if (second == std::string::npos) {
            continue;
        }
        PluginDescription description;
        description.format = "builtin";
        description.id = line.substr(0, first);
        description.name = line.substr(first + 1, second - first - 1);
        description.vendor = line.substr(second + 1);
        description.audioInputs = 2;
        description.audioOutputs = 2;
        emit(description);
    }
    return 0;
}

} // namespace

std::string text(const LilvNode *node)
{
    return node != nullptr && lilv_node_as_string(node) != nullptr ? std::string{lilv_node_as_string(node)}
                                                                   : std::string{};
}

std::vector<plugin::PluginParam> lv2Params(LilvWorld *world, const LilvPlugin *plugin)
{
    LilvNode *controlPort = lilv_new_uri(world, LILV_URI_CONTROL_PORT);
    LilvNode *inputPort = lilv_new_uri(world, LILV_URI_INPUT_PORT);
    LilvNode *toggled = lilv_new_uri(world, LV2_CORE__toggled);
    LilvNode *integer = lilv_new_uri(world, LV2_CORE__integer);
    LilvNode *enumeration = lilv_new_uri(world, LV2_CORE__enumeration);
    LilvNode *logarithmic = lilv_new_uri(world, "http://lv2plug.in/ns/ext/port-props#logarithmic");
    std::vector<plugin::PluginParam> params;
    const std::uint32_t ports = lilv_plugin_get_num_ports(plugin);
    for (std::uint32_t index = 0; index < ports; ++index) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, index);
        if (!lilv_port_is_a(plugin, port, controlPort) || !lilv_port_is_a(plugin, port, inputPort)) {
            continue;
        }
        plugin::PluginParam param;
        param.id = text(lilv_port_get_symbol(plugin, port));
        LilvNode *name = lilv_port_get_name(plugin, port);
        param.name = name != nullptr ? text(name) : param.id;
        lilv_node_free(name);
        LilvNode *defaultValue = nullptr;
        LilvNode *minimum = nullptr;
        LilvNode *maximum = nullptr;
        lilv_port_get_range(plugin, port, &defaultValue, &minimum, &maximum);
        param.minimum = minimum != nullptr ? lilv_node_as_float(minimum) : 0.0f;
        param.maximum = maximum != nullptr ? lilv_node_as_float(maximum) : 1.0f;
        param.defaultValue = defaultValue != nullptr ? lilv_node_as_float(defaultValue) : param.minimum;
        lilv_node_free(defaultValue);
        lilv_node_free(minimum);
        lilv_node_free(maximum);
        if (param.maximum < param.minimum) {
            std::swap(param.minimum, param.maximum);
        }
        param.defaultValue = std::clamp(param.defaultValue, param.minimum, param.maximum);
        param.logarithmic = lilv_port_has_property(plugin, port, logarithmic);
        if (lilv_port_has_property(plugin, port, toggled)) {
            param.kind = plugin::PluginParam::Kind::Toggle;
        } else if (lilv_port_has_property(plugin, port, enumeration)) {
            param.kind = plugin::PluginParam::Kind::Choice;
            if (LilvScalePoints *points = lilv_port_get_scale_points(plugin, port); points != nullptr) {
                LILV_FOREACH(scale_points, it, points)
                {
                    const LilvScalePoint *point = lilv_scale_points_get(points, it);
                    param.choices.emplace_back(lilv_node_as_float(lilv_scale_point_get_value(point)),
                                               text(lilv_scale_point_get_label(point)));
                }
                lilv_scale_points_free(points);
            }
            std::ranges::sort(param.choices, [](const auto &a, const auto &b) { return a.first < b.first; });
            if (param.choices.empty()) {
                param.kind = plugin::PluginParam::Kind::Int;
            }
        } else if (lilv_port_has_property(plugin, port, integer)) {
            param.kind = plugin::PluginParam::Kind::Int;
        }
        params.push_back(std::move(param));
    }
    for (LilvNode *node : {controlPort, inputPort, toggled, integer, enumeration, logarithmic}) {
        lilv_node_free(node);
    }
    return params;
}

namespace
{

/// **An LV2 bundle, read from its Turtle files by lilv**: no plugin code is
/// loaded to describe one. The whole LV2 world is loaded, for the classes
/// and the specifications a bundle's plugins refer to, and what is kept is
/// what lives in this bundle.
int scanLv2(const std::string &path)
{
    LilvWorld *world = lilv_world_new();
    lilv_world_load_all(world);
    std::string bundle = path;
    if (!bundle.ends_with('/')) {
        bundle += '/';
    }
    LilvNode *bundleUri = lilv_new_file_uri(world, nullptr, bundle.c_str());
    LilvNode *audioPort = lilv_new_uri(world, LILV_URI_AUDIO_PORT);
    LilvNode *controlPort = lilv_new_uri(world, LILV_URI_CONTROL_PORT);
    LilvNode *inputPort = lilv_new_uri(world, LILV_URI_INPUT_PORT);
    LilvNode *outputPort = lilv_new_uri(world, LILV_URI_OUTPUT_PORT);

    const LilvPlugins *plugins = lilv_world_get_all_plugins(world);
    LILV_FOREACH(plugins, it, plugins)
    {
        const LilvPlugin *plugin = lilv_plugins_get(plugins, it);
        if (!lilv_node_equals(lilv_plugin_get_bundle_uri(plugin), bundleUri)) {
            continue;
        }
        PluginDescription description;
        description.format = "lv2";
        description.id = text(lilv_plugin_get_uri(plugin));
        LilvNode *name = lilv_plugin_get_name(plugin);
        description.name = text(name);
        lilv_node_free(name);
        LilvNode *author = lilv_plugin_get_author_name(plugin);
        description.vendor = text(author);
        lilv_node_free(author);
        if (const LilvPluginClass *kind = lilv_plugin_get_class(plugin); kind != nullptr) {
            description.category = text(lilv_plugin_class_get_label(kind));
        }
        description.audioInputs = lilv_plugin_get_num_ports_of_class(plugin, audioPort, inputPort, nullptr);
        description.audioOutputs = lilv_plugin_get_num_ports_of_class(plugin, audioPort, outputPort, nullptr);
        description.params = lv2Params(world, plugin);
        description.parameters = static_cast<std::uint32_t>(description.params.size());
        emit(description);
    }
    for (LilvNode *node : {bundleUri, audioPort, controlPort, inputPort, outputPort}) {
        lilv_node_free(node);
    }
    lilv_world_free(world);
    return 0;
}

#ifdef RB_HAVE_CLAP
/// **A CLAP file, described by loading it**: CLAP says what a plugin
/// is only through its code. That is why describing happens in a host of
/// its own. **What the plugins print goes to standard error**, so that
/// nothing a plugin says on being loaded gets into the lines the engine
/// reads.
int scanClap(const std::string &path)
{
    std::fflush(stdout);
    const int lines = ::dup(STDOUT_FILENO);
    ::dup2(STDERR_FILENO, STDOUT_FILENO);
    const bool read = describeClap(path, [lines](const PluginDescription &description) { emit(description, lines); });
    std::fflush(stdout);
    ::dup2(lines, STDOUT_FILENO);
    ::close(lines);
    return read ? 0 : kScanUnreadable;
}
#endif

#ifdef RB_HAVE_VST3
/// **A VST3 bundle, described by loading it**: its `moduleinfo.json`
/// names its plugins, but only their code says what their settings and
/// channels are. What the plugins print goes to standard error, as for
/// CLAP.
int scanVst3(const std::string &path)
{
    std::fflush(stdout);
    const int lines = ::dup(STDOUT_FILENO);
    ::dup2(STDERR_FILENO, STDOUT_FILENO);
    const bool read = describeVst3(path, [lines](const PluginDescription &description) { emit(description, lines); });
    std::fflush(stdout);
    ::dup2(lines, STDOUT_FILENO);
    ::close(lines);
    return read ? 0 : kScanUnreadable;
}
#endif

} // namespace

namespace
{

/// **Every plugin in one LADSPA library**. Loading the library runs
/// its code, which is why this is done here and not in the engine.
int scanLadspa(const std::string &path)
{
    std::string error;
    const std::shared_ptr<LadspaLibrary> library = LadspaLibrary::open(path, error);
    if (!library) {
        std::fprintf(stderr, "not a LADSPA library: %s: %s\n", path.c_str(), error.c_str());
        return kScanUnreadable;
    }
    for (const ladspa::Descriptor *descriptor : library->plugins()) {
        if (descriptor->Label == nullptr) {
            continue;
        }
        emit(describeLadspa(path, *descriptor));
    }
    return 0;
}

} // namespace

int scan(const std::string &format, const std::string &path)
{
    if (format == "builtin") {
        return scanBuiltin(path);
    }
    if (format == "lv2") {
        return scanLv2(path);
    }
    if (format == "ladspa") {
        return scanLadspa(path);
    }
#ifdef RB_HAVE_CLAP
    if (format == "clap") {
        return scanClap(path);
    }
#endif
#ifdef RB_HAVE_VST3
    if (format == "vst3") {
        return scanVst3(path);
    }
#endif
    std::fprintf(stderr, "no describer for the format %s\n", format.c_str());
    return kScanUnreadable;
}

} // namespace rb::pluginhost
