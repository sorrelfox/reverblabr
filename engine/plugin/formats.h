// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb::plugin
{

/// **Something on the disk that may hold plugins**: an LV2 bundle, a
/// CLAP file, a LADSPA library. It is found by looking at names and never
/// by loading anything, so finding one is safe in the engine; what is in it
/// is found in a host process, where a plugin that crashes on being looked
/// at takes only that process with it.
struct Candidate {
    std::string format;
    /// Where it is. For the built-in format's own plugins, "builtin".
    std::string path;
    /// When it last changed, in nanoseconds since the epoch: the latest of
    /// the entry's own time and, for a directory, its immediate contents'.
    /// A candidate whose time has not moved is not looked into again.
    std::int64_t modified = 0;

    [[nodiscard]] bool operator==(const Candidate &other) const = default;
};

/// **One of a plugin's settings**, as the plugin describes it: what
/// the engine builds the plugin block's parameter from, without the plugin's
/// code anywhere near it. In the plugin's own order, which is the order the
/// host numbers them in too.
struct PluginParam {
    enum class Kind : std::uint8_t {
        Float,
        Int,
        Toggle, ///< Off or on.
        Choice, ///< One of `choices`, by position.
    };

    /// The plugin's own stable name for it: an LV2 port's symbol.
    std::string id;
    /// What the plugin calls it, for a person.
    std::string name;
    Kind kind = Kind::Float;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    bool logarithmic = false;
    /// For a choice: its values and what the plugin calls them, by value.
    std::vector<std::pair<float, std::string>> choices;

    [[nodiscard]] bool operator==(const PluginParam &other) const = default;
};

/// **One plugin**, as a scan found it and the library shows it.
struct PluginDescription {
    std::string format;
    /// The format's own id for it: an LV2 URI, a CLAP id, a LADSPA
    /// library and label. What the plugin block's "plugin" setting holds.
    std::string id;
    std::string name;
    /// Who made it, as the plugin says.
    std::string vendor;
    /// The plugin's own word for what it is ("Compressor", "Filter"), or
    /// empty.
    std::string category;
    std::uint32_t audioInputs = 0;
    std::uint32_t audioOutputs = 0;
    std::uint32_t parameters = 0;
    /// What those parameters are.
    std::vector<PluginParam> params;
    /// **Where it was found**: the bundle, file or library a scan looked
    /// into. What a host is told to load for a format whose id does not
    /// say where the plugin is, as a CLAP id does not.
    std::string location;

    [[nodiscard]] bool operator==(const PluginDescription &other) const = default;
};

/// **Where the engine asks what a plugin is**: its description, from
/// what a scan found, or nothing for one this machine has not got.
///
/// A plugin block is made by a block registry that hands it nothing, and it
/// learns which plugin it is only from a setting; this is how it then finds
/// the plugin's parameters without loading the plugin. The engine sets it
/// to its plugin cache; nothing set answers nothing.
using PluginInfoSource = std::function<std::optional<PluginDescription>(std::string_view format, std::string_view id)>;
void setPluginInfoSource(PluginInfoSource source);
[[nodiscard]] std::optional<PluginDescription> describePlugin(std::string_view format, std::string_view id);

/// One line of JSON for \a description: what a scanning host writes, one
/// plugin a line.
[[nodiscard]] std::string toJsonLine(const PluginDescription &description);

/// **A plugin format, as far as finding its plugins goes**.
///
/// Each format the host can load says where its plugins live and which
/// entries there are candidates. **A new format is a new one of these**, a
/// describer in the host and a loader: the scan, its cache and the
/// library take whatever the formats find, and need nothing of their own.
class PluginFormat
{
public:
    virtual ~PluginFormat() = default;

    /// "builtin", "lv2", "clap", "ladspa": the plugin block's type is
    /// `plugin.<name>`.
    [[nodiscard]] virtual std::string_view name() const = 0;
    /// The folders its plugins are in, in the order the format says to
    /// look, whether or not they exist.
    [[nodiscard]] virtual std::vector<std::filesystem::path> searchPaths() const = 0;
    /// Whether an entry of one of those folders is a candidate.
    [[nodiscard]] virtual bool isCandidate(const std::filesystem::directory_entry &entry) const = 0;
    /// Whether candidates may be further down than the folders' own
    /// entries.
    [[nodiscard]] virtual bool searchesSubfolders() const
    {
        return false;
    }
    /// Candidates that are not on the disk at all.
    [[nodiscard]] virtual std::vector<Candidate> builtIn() const
    {
        return {};
    }
};

/// Every format this build can find plugins of.
[[nodiscard]] std::span<const std::unique_ptr<PluginFormat>> formats();

/// The format called \a name, or null.
[[nodiscard]] const PluginFormat *formatNamed(std::string_view name);

/// Every candidate of \a format, in a stable order.
[[nodiscard]] std::vector<Candidate> candidatesOf(const PluginFormat &format);

/// The built-in format's test candidates live in the folders this names, as
/// `*.rbtest` files: a plugin that crashes or hangs when it is looked at,
/// which is what a scan has to survive.
inline constexpr const char *kTestPluginPathVariable = "REVERBLABR_TEST_PLUGIN_PATH";

/// **CLAP's own folders** are searched as well as those `CLAP_PATH`
/// names, as the CLAP specification says; this names the folders to search
/// instead of them, for a test that must not see the machine's plugins.
inline constexpr const char *kClapDefaultPathVariable = "REVERBLABR_CLAP_DEFAULT_PATH";

/// The paths a colon-separated variable names, or \a fallback when it is
/// unset or empty. A leading `~/` is the home folder.
[[nodiscard]] std::vector<std::filesystem::path> pathsFrom(const char *variable,
                                                           const std::vector<std::string> &fallback);

} // namespace rb::plugin
