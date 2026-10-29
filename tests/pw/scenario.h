// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// A session in a real engine, built the way the window builds one, and the
// tones that feed it: shared by the scenarios M1 is judged by and by the
// long runs (test_soak.cpp), which build bigger sessions the same way.

#include "blocks/builtinblocks.h"
#include "blocks/dynamics.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/sessionruntime.h"
#include "core/telemetry.h"
#include "dsp/blockregistry.h"
#include "sessionharness.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb::pwtest
{

using namespace rb::core;

[[nodiscard]] inline dsp::BlockRegistry builtinRegistry()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(blocks::registerBuiltinBlocks(registry));
    return registry;
}

[[nodiscard]] inline QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline QString typeName(std::string_view type)
{
    return QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
}

/// Settings for the Dynamics block with every part of the curve named, so
/// that a test says which curve it is judging rather than inheriting the
/// block's defaults — which are a hard limiter at −20 dB and would say
/// little about anything.
[[nodiscard]] inline QJsonObject dynamicsSettings(double thresholdDb, double ratio)
{
    return QJsonObject{
        {"threshold"_L1, thresholdDb},
        {"ratio"_L1, ratio},
        {"knee"_L1, 0.0},
        {"makeup"_L1, 0.0},
        {"attack"_L1, 1.0},
        {"release"_L1, 100.0},
        {"detector"_L1, typeName(blocks::Dynamics::kDetectorPeak)},
    };
}

[[nodiscard]] inline std::optional<BlockTelemetry> blockOf(const TelemetrySnapshot &snapshot, const QString &id)
{
    const QByteArray wanted = id.toUtf8();
    for (std::uint32_t index = 0; index < snapshot.blockCount; ++index) {
        if (wanted == snapshot.blocks[index].id) {
            return snapshot.blocks[index];
        }
    }
    return std::nullopt;
}

/// A capture filter that calls itself a sink, so that an Output Device block
/// can be pointed at it and what arrived can be measured.
[[nodiscard]] inline CaptureNodeOptions deviceOptions(const std::string &name, double sineHz = 0.0)
{
    CaptureNodeOptions options = captureOptions(name, sineHz);
    options.mediaClass = "Audio/Sink";
    return options;
}

[[nodiscard]] inline FakeAppOptions toneApp(const std::string &name, const std::string &binary, double hz)
{
    FakeAppOptions options = appOptions(name, binary);
    options.toneHz = hz;
    return options;
}

/// One session in a real engine, built the way the window builds it.
///
/// **Only the interface.** Blocks go in as AddBlock edits with their
/// settings, wires as Connect edits, one channel at a time; the session is
/// started and read through the facade and its telemetry slot. Nothing here
/// reaches past what the GUI can reach, except to find the node in the graph
/// so the test can wait for it.
class Scenario
{
public:
    explicit Scenario(const QString &name)
        : m_registry(builtinRegistry())
        , m_engine(m_registry, SessionStore{m_sessions.path()})
        , m_opened(&m_engine, &EngineFacade::sessionOpened)
        , m_errors(&m_engine, &EngineFacade::error)
        , m_name(name)
    {
    }

    /// Connects, and makes the session.
    [[nodiscard]] bool open()
    {
        m_engine.connectToPipeWire();
        if (!m_pump.pumpUntil([this] { return m_engine.isConnected(); })) {
            return false;
        }
        m_engine.createSession(m_name);
        if (m_opened.isEmpty()) {
            return false;
        }
        m_id = m_opened.back().at(0).toString();
        return true;
    }

    void add(const QString &blockId, std::string_view type, const QJsonObject &params = {})
    {
        BlockInstance block;
        block.id = blockId;
        block.type = typeName(type);
        block.params = params;
        AddBlock command;
        command.block = block;
        m_engine.applyEdit(m_id, asJson(editCommandToJson(command)));
    }

    /// A setting changed while it runs, as a panel changes one.
    void setParam(const QString &blockId, const QString &key, const QJsonValue &value)
    {
        m_engine.applyEdit(m_id, asJson(editCommandToJson(SetParam{blockId, key, value})));
    }

    /// A stereo link, which on the canvas is one Ctrl-drag.
    void link(const QString &from, const QString &to)
    {
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            Wire wire;
            wire.fromBlock = from;
            wire.fromPort = channel;
            wire.toBlock = to;
            wire.toPort = channel;
            m_engine.applyEdit(m_id, asJson(editCommandToJson(Connect{wire, -1})));
        }
    }

    /// Several settings of one block as one edit, in order, as a Mixer
    /// fade is sent.
    void setParams(const QString &blockId, const std::vector<std::pair<QString, QJsonValue>> &changes)
    {
        QJsonArray commands;
        for (const auto &[key, value] : changes) {
            commands.append(editCommandToJson(SetParam{blockId, key, value}));
        }
        m_engine.applyEdit(m_id, QString::fromUtf8(QJsonDocument(commands).toJson(QJsonDocument::Compact)));
    }

    /// A stereo link into one lane of a block with several, which on
    /// the canvas is a Ctrl-drag let go over that lane's connector.
    void linkInto(const QString &from, const QString &to, std::uint32_t firstPort)
    {
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            Wire wire;
            wire.fromBlock = from;
            wire.fromPort = channel;
            wire.toBlock = to;
            wire.toPort = firstPort + channel;
            m_engine.applyEdit(m_id, asJson(editCommandToJson(Connect{wire, -1})));
        }
    }

    /// Takes a block out, and every wire on it with it.
    void remove(const QString &blockId)
    {
        RemoveBlock command;
        command.blockId = blockId;
        m_engine.applyEdit(m_id, asJson(editCommandToJson(command)));
    }

    /// Links the blocks one after another, as a lane.
    void lane(const QStringList &blocks)
    {
        for (qsizetype index = 1; index < blocks.size(); ++index) {
            link(blocks.at(index - 1), blocks.at(index));
        }
    }

    /// Starts it and waits for its node to be in the graph.
    [[nodiscard]] std::optional<Graph::Node> start()
    {
        m_engine.start(m_id);
        if (m_engine.runtimeOf(m_id) == nullptr || m_engine.runtimeOf(m_id)->node() == nullptr) {
            return std::nullopt;
        }
        return m_pump.awaitNode(m_engine.runtimeOf(m_id)->node()->nodeName());
    }

    void stop()
    {
        m_engine.stop(m_id);
    }

    /// What the engine has said went wrong, as one line, or empty.
    [[nodiscard]] std::string errors() const
    {
        std::string said;
        for (const QList<QVariant> &error : m_errors) {
            said += error.at(0).toString().toStdString() + " " + error.at(1).toString().toStdString() + "; ";
        }
        return said;
    }

    /// What the interface would read for \a blockId now.
    [[nodiscard]] std::optional<BlockTelemetry> block(const QString &blockId) const
    {
        const TelemetrySlot *slot = m_engine.telemetryOf(m_id);
        return slot == nullptr ? std::nullopt : blockOf(slot->load(), blockId);
    }

    [[nodiscard]] TelemetrySnapshot snapshot() const
    {
        const TelemetrySlot *slot = m_engine.telemetryOf(m_id);
        return slot == nullptr ? TelemetrySnapshot{} : slot->load();
    }

    /// Whether every one of \a blocks is showing a signal, which is what
    /// lights the wires and the state dots.
    [[nodiscard]] bool allLive(const QStringList &blocks) const
    {
        return std::ranges::all_of(blocks, [this](const QString &blockId) {
            const std::optional<BlockTelemetry> reading = block(blockId);
            return reading.has_value() && reading->active;
        });
    }

    /// The first of \a blocks that is not showing a signal, for a message.
    [[nodiscard]] std::string firstQuiet(const QStringList &blocks) const
    {
        for (const QString &blockId : blocks) {
            const std::optional<BlockTelemetry> reading = block(blockId);
            if (!reading.has_value() || !reading->active) {
                return blockId.toStdString();
            }
        }
        return {};
    }

    [[nodiscard]] SessionRuntime *runtime() const
    {
        return m_engine.runtimeOf(m_id);
    }

    Waiter &pump()
    {
        return m_pump;
    }

    /// The engine itself, for a signal the facade sends rather than a
    /// reading, such as a recording's file being finished.
    [[nodiscard]] Engine &engine()
    {
        return m_engine;
    }

    [[nodiscard]] const QString &id() const
    {
        return m_id;
    }

private:
    QTemporaryDir m_sessions;
    dsp::BlockRegistry m_registry;
    Engine m_engine;
    QSignalSpy m_opened;
    QSignalSpy m_errors;
    Waiter m_pump;
    QString m_name;
    QString m_id;
};

/// A tone played into one of the harness's virtual microphones.
///
/// **The policy will not route a playback stream into a source**, whatever
/// it is made of, so the stream is linked by hand: that is what feeding a
/// microphone that does not exist looks like from outside, and it is the
/// harness's business rather than the engine's. It is played at nothing
/// else, so no other block can pick it up on the way.
class Microphone
{
public:
    Microphone(Waiter &pump, const char *source, const std::string &binary, double hz)
        : m_app(options(source, binary, hz))
    {
        const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf(binary));
        const std::optional<Graph::Node> device = pump.awaitNode(QString::fromLatin1(source));
        if (!stream || !device || !pump.awaitPorts(stream->id, "output", 2)) {
            return;
        }
        const Graph graph = Graph::capture();
        const std::vector<Graph::Port> from = graph.portsOf(stream->id, "output");
        const std::vector<Graph::Port> into = graph.portsOf(device->id, "input");
        if (into.size() >= 2) {
            m_fed = linkByChannel(stream->id, device->id) >= 2;
        } else if (!into.empty() && !from.empty()) {
            // A mono microphone records the left side of the tone.
            m_fed = linkPorts(from.front().id, into.front().id);
        }
    }

    [[nodiscard]] bool fed() const
    {
        return m_fed;
    }

    /// Whether the tone is still playing: it lasts a minute, and a run
    /// longer than that plays a new one.
    [[nodiscard]] bool playing() const
    {
        return m_app.running();
    }

private:
    [[nodiscard]] static FakeAppOptions options(const char *source, const std::string &binary, double hz)
    {
        FakeAppOptions made = toneApp(binary, binary, hz);
        made.target = source;
        return made;
    }

    FakeApp m_app;
    bool m_fed = false;
};

/// The CPU time this process has used, in seconds.
[[nodiscard]] inline double processSeconds()
{
    timespec now{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

/// Runs the event loop for \a duration **without spinning**: Waiter::pump
/// polls, which is right for waiting on a condition and wrong for measuring
/// what anything else costs, because the polling would be most of it.
inline void idleFor(std::chrono::milliseconds duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace rb::pwtest
