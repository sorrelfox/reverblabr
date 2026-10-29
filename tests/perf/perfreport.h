// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// What a benchmark or a long run measured, written as JSON for
// tools/perf/report.py to hold against a baseline.
//
// **Plain C++ and nothing else**, so the DSP benchmark, which links no Qt,
// writes the same file the soak and the canvas do. The format is in
// docs/development.md ("Soak and performance runs").

#include <sys/utsname.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace rb::perf
{

/// Which way a figure gets better, so a change can be called a regression.
/// Neither is a figure kept for the record, such as how long a run lasted.
enum class Better {
    Lower,
    Higher,
    Neither
};

struct Metric {
    std::string name;
    double value = 0.0;
    std::string unit;
    Better better = Better::Lower;
    /// How far, as a share of the baseline, the figure may move the wrong
    /// way before it counts as a regression. Unset leaves it to the report's
    /// default.
    std::optional<double> tolerance;
    /// A change smaller than this, in the figure's own unit, is never a
    /// regression: a block that costs a hundredth of a per cent of a core
    /// and now costs two is not news.
    std::optional<double> noise;
    std::string detail;
};

/// Escapes \a text for a JSON string.
[[nodiscard]] inline std::string jsonString(std::string_view text)
{
    std::string out = "\"";
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
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
                out += escaped;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

/// A number as JSON has it: no NaN or infinity, which JSON cannot hold.
[[nodiscard]] inline std::string jsonNumber(double value)
{
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream out;
    out.precision(6);
    out << value;
    return out.str();
}

/// The processor's name, as the kernel gives it, so two runs on different
/// machines are not compared without anyone noticing.
[[nodiscard]] inline std::string cpuModel()
{
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.starts_with("model name")) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                return line.substr(line.find_first_not_of(' ', colon + 1));
            }
        }
    }
    return "unknown";
}

/// Whether the figures came from an optimised build. A debug build is
/// several times slower, and its figures are no baseline for anything.
[[nodiscard]] constexpr std::string_view buildKind()
{
#ifdef NDEBUG
    return "release";
#else
    return "debug";
#endif
}

class Report
{
public:
    explicit Report(std::string suite)
        : m_suite(std::move(suite))
    {
    }

    void add(Metric metric)
    {
        m_metrics.push_back(std::move(metric));
    }

    /// Something about the run that is not a figure, such as the platform
    /// the canvas was drawn on.
    void note(std::string key, std::string value)
    {
        m_notes.emplace_back(std::move(key), std::move(value));
    }

    [[nodiscard]] const std::vector<Metric> &metrics() const
    {
        return m_metrics;
    }

    [[nodiscard]] std::string toJson() const
    {
        std::ostringstream out;
        utsname system{};
        uname(&system);
        const std::time_t now = std::time(nullptr);
        char when[32];
        std::tm utc{};
        gmtime_r(&now, &utc);
        std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &utc);

        out << "{\n  \"schema\": 1,\n  \"suite\": " << jsonString(m_suite)
            << ",\n  \"build\": " << jsonString(buildKind()) << ",\n  \"when\": " << jsonString(when)
            << ",\n  \"host\": {\"cpu\": " << jsonString(cpuModel())
            << ", \"cores\": " << std::thread::hardware_concurrency() << ", \"kernel\": " << jsonString(system.release)
            << ", \"machine\": " << jsonString(system.machine) << "},\n  \"notes\": {";
        for (std::size_t index = 0; index < m_notes.size(); ++index) {
            out << (index == 0 ? "" : ", ") << jsonString(m_notes[index].first) << ": "
                << jsonString(m_notes[index].second);
        }
        out << "},\n  \"metrics\": {";
        for (std::size_t index = 0; index < m_metrics.size(); ++index) {
            const Metric &metric = m_metrics[index];
            out << (index == 0 ? "\n" : ",\n") << "    " << jsonString(metric.name)
                << ": {\"value\": " << jsonNumber(metric.value) << ", \"unit\": " << jsonString(metric.unit)
                << ", \"better\": "
                << jsonString(metric.better == Better::Lower        ? "lower"
                                  : metric.better == Better::Higher ? "higher"
                                                                    : "neither");
            if (metric.tolerance) {
                out << ", \"tolerance\": " << jsonNumber(*metric.tolerance);
            }
            if (metric.noise) {
                out << ", \"noise\": " << jsonNumber(*metric.noise);
            }
            if (!metric.detail.empty()) {
                out << ", \"detail\": " << jsonString(metric.detail);
            }
            out << "}";
        }
        out << "\n  }\n}\n";
        return out.str();
    }

    /// Writes the report to \a path, making its folder. Answers whether it
    /// was written.
    [[nodiscard]] bool write(const std::filesystem::path &path) const
    {
        std::error_code error;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), error);
        }
        std::ofstream file(path);
        file << toJson();
        return static_cast<bool>(file);
    }

    /// Writes it as \a fileName under RB_PERF_OUT, where a run that wants
    /// the figures asks for them. Without the variable nothing is written
    /// and the answer is true: a test run by hand prints its figures anyway.
    [[nodiscard]] bool writeToOutputFolder(const std::string &fileName) const
    {
        const char *folder = std::getenv("RB_PERF_OUT");
        if (folder == nullptr || *folder == '\0') {
            return true;
        }
        return write(std::filesystem::path(folder) / fileName);
    }

private:
    std::string m_suite;
    std::vector<std::pair<std::string, std::string>> m_notes;
    std::vector<Metric> m_metrics;
};

} // namespace rb::perf
