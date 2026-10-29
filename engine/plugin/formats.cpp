// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "plugin/formats.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace rb::plugin
{

namespace fs = std::filesystem;

namespace
{

/// The built-in plugins' own candidate changes only when this does: a build
/// that adds one says so here.
constexpr std::int64_t kBuiltinVersion = 2;

[[nodiscard]] std::int64_t nanosOf(fs::file_time_type time)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
}

[[nodiscard]] std::int64_t modifiedOf(const fs::directory_entry &entry)
{
    std::error_code error;
    std::int64_t latest = nanosOf(entry.last_write_time(error));
    if (entry.is_directory(error)) {
        for (const fs::directory_entry &inside : fs::directory_iterator(entry.path(), error)) {
            std::error_code ignored;
            latest = std::max(latest, nanosOf(inside.last_write_time(ignored)));
        }
    }
    return latest;
}

[[nodiscard]] bool endsWith(const fs::path &path, std::string_view suffix)
{
    const std::string name = path.filename().string();
    return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// The plugins built into the host (test.gain and the rest), and the test
/// candidates a scan has to survive.
class BuiltinFormat final : public PluginFormat
{
public:
    [[nodiscard]] std::string_view name() const override
    {
        return "builtin";
    }

    [[nodiscard]] std::vector<fs::path> searchPaths() const override
    {
        return pathsFrom(kTestPluginPathVariable, {});
    }

    [[nodiscard]] bool isCandidate(const fs::directory_entry &entry) const override
    {
        std::error_code error;
        return entry.is_regular_file(error) && endsWith(entry.path(), ".rbtest");
    }

    [[nodiscard]] std::vector<Candidate> builtIn() const override
    {
        return {Candidate{"builtin", "builtin", kBuiltinVersion}};
    }
};

/// LV2: a bundle is a folder ending in `.lv2`, in the folders `LV2_PATH`
/// names or, without it, the ones lilv looks in.
class Lv2Format final : public PluginFormat
{
public:
    [[nodiscard]] std::string_view name() const override
    {
        return "lv2";
    }

    [[nodiscard]] std::vector<fs::path> searchPaths() const override
    {
        return pathsFrom("LV2_PATH",
                         {"~/.lv2", "/usr/local/lib64/lv2", "/usr/local/lib/lv2", "/usr/lib64/lv2", "/usr/lib/lv2"});
    }

    [[nodiscard]] bool isCandidate(const fs::directory_entry &entry) const override
    {
        std::error_code error;
        return entry.is_directory(error) && endsWith(entry.path(), ".lv2");
    }
};

/// **LADSPA**: shared libraries, each with one or more plugins in it,
/// in the folders LADSPA_PATH names, or where distributions put them.
class LadspaFormat final : public PluginFormat
{
public:
    [[nodiscard]] std::string_view name() const override
    {
        return "ladspa";
    }

    [[nodiscard]] std::vector<fs::path> searchPaths() const override
    {
        return pathsFrom(
            "LADSPA_PATH",
            {"~/.ladspa", "/usr/local/lib64/ladspa", "/usr/local/lib/ladspa", "/usr/lib64/ladspa", "/usr/lib/ladspa"});
    }

    [[nodiscard]] bool isCandidate(const fs::directory_entry &entry) const override
    {
        std::error_code error;
        return entry.is_regular_file(error) && endsWith(entry.path(), ".so");
    }
};

#ifdef RB_HAVE_CLAP
/// CLAP: a plugin file ends in `.clap`, anywhere under `~/.clap` and
/// the system's CLAP folders, and under the folders `CLAP_PATH` names —
/// those as well, not instead, as the CLAP specification says.
class ClapFormat final : public PluginFormat
{
public:
    [[nodiscard]] std::string_view name() const override
    {
        return "clap";
    }

    [[nodiscard]] std::vector<fs::path> searchPaths() const override
    {
        std::vector<fs::path> paths = pathsFrom("CLAP_PATH", {});
        for (fs::path &path : pathsFrom(
                 kClapDefaultPathVariable,
                 {"~/.clap", "/usr/local/lib64/clap", "/usr/local/lib/clap", "/usr/lib64/clap", "/usr/lib/clap"})) {
            paths.push_back(std::move(path));
        }
        return paths;
    }

    [[nodiscard]] bool isCandidate(const fs::directory_entry &entry) const override
    {
        std::error_code error;
        return entry.is_regular_file(error) && endsWith(entry.path(), ".clap");
    }

    [[nodiscard]] bool searchesSubfolders() const override
    {
        return true;
    }
};
#endif

#ifdef RB_HAVE_VST3
/// **VST3**: a plugin is a bundle, a folder ending in `.vst3`, anywhere
/// under `~/.vst3` and the system's VST3 folders, and under the folders
/// `VST3_PATH` names, which other Linux hosts read too: those as well,
/// not instead. A bundle may sit in a vendor's folder of its own.
class Vst3Format final : public PluginFormat
{
public:
    [[nodiscard]] std::string_view name() const override
    {
        return "vst3";
    }

    [[nodiscard]] std::vector<fs::path> searchPaths() const override
    {
        std::vector<fs::path> paths = pathsFrom("VST3_PATH", {});
        for (fs::path &path : pathsFrom(
                 kVst3DefaultPathVariable,
                 {"~/.vst3", "/usr/local/lib64/vst3", "/usr/local/lib/vst3", "/usr/lib64/vst3", "/usr/lib/vst3"})) {
            paths.push_back(std::move(path));
        }
        return paths;
    }

    [[nodiscard]] bool isCandidate(const fs::directory_entry &entry) const override
    {
        std::error_code error;
        return entry.is_directory(error) && endsWith(entry.path(), ".vst3");
    }

    [[nodiscard]] bool searchesSubfolders() const override
    {
        return true;
    }
};
#endif

[[nodiscard]] std::vector<std::unique_ptr<PluginFormat>> makeFormats()
{
    std::vector<std::unique_ptr<PluginFormat>> made;
    made.push_back(std::make_unique<BuiltinFormat>());
    made.push_back(std::make_unique<Lv2Format>());
    made.push_back(std::make_unique<LadspaFormat>());
#ifdef RB_HAVE_CLAP
    made.push_back(std::make_unique<ClapFormat>());
#endif
#ifdef RB_HAVE_VST3
    made.push_back(std::make_unique<Vst3Format>());
#endif
    return made;
}

void appendEscaped(std::string &out, std::string_view text)
{
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                std::array<char, 8> escaped{};
                std::snprintf(escaped.data(), escaped.size(), "\\u%04x", static_cast<unsigned>(c));
                out += escaped.data();
            } else {
                out += c;
            }
        }
    }
}

void appendField(std::string &out, std::string_view key, std::string_view value)
{
    out += '"';
    out += key;
    out += "\":\"";
    appendEscaped(out, value);
    out += '"';
}

void appendField(std::string &out, std::string_view key, std::uint32_t value)
{
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

void appendNumber(std::string &out, std::string_view key, float value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.9g", static_cast<double>(value));
    out += '"';
    out += key;
    out += "\":";
    out += text.data();
}

[[nodiscard]] std::string_view kindName(PluginParam::Kind kind)
{
    switch (kind) {
    case PluginParam::Kind::Int:
        return "int";
    case PluginParam::Kind::Toggle:
        return "toggle";
    case PluginParam::Kind::Choice:
        return "choice";
    case PluginParam::Kind::Float:
        break;
    }
    return "float";
}

PluginInfoSource &source()
{
    static PluginInfoSource installed;
    return installed;
}

} // namespace

void setPluginInfoSource(PluginInfoSource installed)
{
    source() = std::move(installed);
}

std::optional<PluginDescription> describePlugin(std::string_view format, std::string_view id)
{
    return source() ? source()(format, id) : std::nullopt;
}

std::string toJsonLine(const PluginDescription &description)
{
    std::string line = "{";
    appendField(line, "format", description.format);
    line += ',';
    appendField(line, "id", description.id);
    line += ',';
    appendField(line, "name", description.name);
    line += ',';
    appendField(line, "vendor", description.vendor);
    line += ',';
    appendField(line, "category", description.category);
    line += ',';
    appendField(line, "audioInputs", description.audioInputs);
    line += ',';
    appendField(line, "audioOutputs", description.audioOutputs);
    line += ',';
    appendField(line, "parameters", description.parameters);
    line += ",\"params\":[";
    for (std::size_t index = 0; index < description.params.size(); ++index) {
        const PluginParam &param = description.params[index];
        line += index == 0 ? "{" : ",{";
        appendField(line, "id", param.id);
        line += ',';
        appendField(line, "name", param.name);
        line += ',';
        appendField(line, "kind", kindName(param.kind));
        line += ',';
        appendNumber(line, "min", param.minimum);
        line += ',';
        appendNumber(line, "max", param.maximum);
        line += ',';
        appendNumber(line, "default", param.defaultValue);
        line += param.logarithmic ? ",\"log\":true" : ",\"log\":false";
        if (!param.choices.empty()) {
            line += ",\"choices\":[";
            for (std::size_t choice = 0; choice < param.choices.size(); ++choice) {
                line += choice == 0 ? "{" : ",{";
                appendNumber(line, "value", param.choices[choice].first);
                line += ',';
                appendField(line, "label", param.choices[choice].second);
                line += '}';
            }
            line += ']';
        }
        line += '}';
    }
    line += "]}\n";
    return line;
}

std::span<const std::unique_ptr<PluginFormat>> formats()
{
    static const std::vector<std::unique_ptr<PluginFormat>> all = makeFormats();
    return all;
}

const PluginFormat *formatNamed(std::string_view name)
{
    for (const std::unique_ptr<PluginFormat> &format : formats()) {
        if (format->name() == name) {
            return format.get();
        }
    }
    return nullptr;
}

std::vector<fs::path> pathsFrom(const char *variable, const std::vector<std::string> &fallback)
{
    std::vector<std::string> named;
    if (const char *value = std::getenv(variable); value != nullptr && *value != '\0') {
        std::string_view rest{value};
        while (!rest.empty()) {
            const std::size_t colon = rest.find(':');
            named.emplace_back(rest.substr(0, colon));
            rest = colon == std::string_view::npos ? std::string_view{} : rest.substr(colon + 1);
        }
    } else {
        named = fallback;
    }
    std::vector<fs::path> paths;
    const char *home = std::getenv("HOME");
    for (const std::string &entry : named) {
        if (entry.empty()) {
            continue;
        }
        if (entry.starts_with("~/") && home != nullptr) {
            paths.push_back(fs::path{home} / entry.substr(2));
        } else {
            paths.emplace_back(entry);
        }
    }
    return paths;
}

std::vector<Candidate> candidatesOf(const PluginFormat &format)
{
    std::vector<Candidate> found = format.builtIn();
    const auto consider = [&](const fs::directory_entry &entry) {
        if (format.isCandidate(entry)) {
            found.push_back(Candidate{std::string{format.name()}, entry.path().string(), modifiedOf(entry)});
        }
    };
    for (const fs::path &root : format.searchPaths()) {
        std::error_code error;
        if (!fs::is_directory(root, error)) {
            continue;
        }
        if (format.searchesSubfolders()) {
            for (fs::recursive_directory_iterator walk(root, fs::directory_options::skip_permission_denied, error);
                 !error && walk != fs::recursive_directory_iterator();
                 walk.increment(error)) {
                // **A candidate folder is not looked into**: what is inside
                // a VST3 bundle is the plugin's own.
                if (format.isCandidate(*walk)) {
                    consider(*walk);
                    walk.disable_recursion_pending();
                }
            }
        } else {
            for (const fs::directory_entry &entry : fs::directory_iterator(root, error)) {
                consider(entry);
            }
        }
    }
    // The same candidate found twice — a folder named twice in a path — is
    // one candidate.
    std::ranges::sort(found, [](const Candidate &a, const Candidate &b) { return a.path < b.path; });
    const auto duplicates =
        std::ranges::unique(found, [](const Candidate &a, const Candidate &b) { return a.path == b.path; });
    found.erase(duplicates.begin(), duplicates.end());
    return found;
}

} // namespace rb::plugin
