// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/session.h"
#include "dsp/block.h"
#include "dsp/blockregistry.h"
#include "graph/graphspec.h"
#include "pw/sessionnode.h"
#include "rt/meter.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

namespace rb::pw
{
class Connection;
class PluginHostProcess;
class RegistryMirror;
} // namespace rb::pw

namespace rb::core
{

/// Why a session could not be run.
enum class RuntimeError : std::uint8_t {
    UnknownBlockType, ///< The session names a block this build does not have.
    BlockRefused, ///< A block would not get ready for this rate or cycle.
    NotCompilable, ///< The blocks and wires do not make a graph that can run.
    NodeRefused, ///< PipeWire would not take the node.
    NotRunning, ///< It was asked to do something that only makes sense while running.
};

[[nodiscard]] QString runtimeErrorName(RuntimeError error);

/// What a parameter change still needs before it can be heard.
enum class ParamEffect : std::uint8_t {
    /// Nothing more. The block has the value, and it has gone to the audio
    /// thread through the parameter queue where that is how it travels.
    Applied,
    /// The parameter is not automatable, so the session has to be planned
    /// again — other links, other ports — before the change is audible. The
    /// runtime cannot do that itself: the session it holds is the one it was
    /// last given, and the value that changed is in the caller's.
    NeedsReplan,
};

/// What a plugin block's host is doing, as the interface is told it
/// through the block's published value PluginBlock::kHostStateValue.
enum class PluginHealth : std::uint8_t {
    NotHosted = 0, ///< A plugin block switched off, or a session not running.
    Starting = 1, ///< Its host has been started and is not ready yet.
    Running = 2,
    Crashed = 3, ///< Its host went; the block is linked around.
    Hung = 4, ///< Its host stopped running and was killed; linked around.
};

/// One session, running.
///
/// **This is what turns a saved session into sound.** It makes a block for
/// every block instance in the session, compiles them into a graph, gives
/// PipeWire a node with a port for every source and every output, and keeps
/// all of that true as the session is edited ([ADR-0008]).
///
/// **The blocks belong to the runtime, not to the graph.** That is what lets
/// a recompile keep what they remember: an envelope part-way through its
/// decay stays part-way through it, because it is the same object.
///
/// **A block whose type this build does not have is not a failure.** The
/// session keeps it exactly as it was so that saving cannot lose it; the
/// runtime simply cannot run a session that contains one, and says which.
///
/// Live on the engine thread. Nothing here may be called from the audio
/// thread, and nothing here blocks the thread that calls it for longer than
/// an edit's fade.
class SessionRuntime : public QObject
{
    Q_OBJECT

public:
    /// All three must outlive the runtime. The mirror is what says which
    /// applications are playing, which is what a source block captures.
    SessionRuntime(pw::Connection &connection,
                   pw::RegistryMirror &mirror,
                   const dsp::BlockRegistry &registry,
                   QObject *parent = nullptr);
    ~SessionRuntime() override;

    /// Makes the blocks, compiles the graph and starts the node. The session
    /// is remembered, so later edits are differences against it.
    [[nodiscard]] std::expected<void, RuntimeError> start(const Session &session);

    /// Takes the node, its ports and its links out of the graph. Keeps the
    /// blocks, so starting again does not begin from nothing.
    void stop();

    [[nodiscard]] bool isRunning() const;

    /// Makes the running session match \a session: new blocks are made, old
    /// ones let go, the graph is recompiled and handed over, and the lanes
    /// the change can be heard through are faded. Blocks that are in both
    /// keep everything they remember.
    [[nodiscard]] std::expected<void, RuntimeError> update(const Session &session);

    /// Sets one parameter on a running block, by the ids the session uses.
    /// The number behind an automatable parameter goes by the real-time
    /// queue and is smoothed inside the block. Says so when the parameter
    /// was one the session plan reads, because then the block holding the
    /// new value is only half of the change.
    /// \a value is the JSON the facade was given; what it means is read
    /// through the block's own parameter list, which is the only thing that
    /// knows whether a string is an enum, a device or free text.
    [[nodiscard]] std::expected<ParamEffect, dsp::BlockError>
    setParam(const QString &blockId, const QString &paramId, const QJsonValue &value);

    /// **Pause, resume or split a Recorder's recording**: \a action is
    /// "pause", "resume" or "split", applied at the frame its tap has got to
    /// now. The error is the reason it was refused, in the words the
    /// interface's table has: NotRunning, blockNotFound, notARecorder or
    /// unknownAction.
    [[nodiscard]] std::expected<void, QString> controlRecording(const QString &blockId, const QString &action);

    /// How many times the running session has been planned again since it
    /// started. **What a test can count**: a knob that is being dragged
    /// must not raise it, or every mouse move would recompile the graph.
    [[nodiscard]] std::uint64_t replanCount() const;

    /// The node, for a caller that needs to name its ports — the link
    /// manager does. Null before start(). **The first segment's**, for a
    /// session cut around its plugins (ADR-0014); nodes() has them all.
    [[nodiscard]] pw::SessionNode *node() const;

    /// Every segment's node, first to last.
    [[nodiscard]] std::vector<pw::SessionNode *> nodes() const;

    /// How much the session holds the sound back, in samples: its longest
    /// path's latency, through every segment and every plugin.
    [[nodiscard]] std::uint32_t latencySamples() const;

    /// A plugin block's host, or null for a block that has none — one that
    /// is not a plugin, or is switched off.
    [[nodiscard]] pw::PluginHostProcess *pluginHost(const QString &blockId) const;

    /// What \a blockId's host is doing.
    [[nodiscard]] PluginHealth pluginHealth(const QString &blockId) const;

    /// **Starts a plugin again whose host failed**, or restarts one
    /// that is running. It stays linked around until the new host is ready,
    /// and gets back its state and every value set since. The error is the
    /// reason it was refused: NotRunning, blockNotFound or notAHostedPlugin.
    [[nodiscard]] std::expected<void, QString> restartPlugin(const QString &blockId);

    /// How long a host has to have run for its failure to start another by
    /// itself; one that fails sooner is left for somebody to restart. Ten
    /// seconds; tests change it.
    void setPluginRestartWindow(std::chrono::milliseconds window);

    /// What the blocks have to say about themselves, for saving: every
    /// parameter that is not read-only, by block id, as the session file
    /// spells them.
    [[nodiscard]] QHash<QString, QJsonObject> blockState() const;

    /// One block's meter, and enough to say which block it is.
    struct MeteredBlock {
        QString id;
        rt::BlockMeter *meter = nullptr;
        bool enabled = true;
        /// How late the block makes the sound, in samples: what its
        /// tile says, and what the paths beside it are held back by.
        std::uint32_t latencySamples = 0;
    };

    /// Every block that is running, in the order the graph holds them, with
    /// where its levels are accumulated. **For the telemetry sampler**,
    /// which empties them sixty times a second; nothing else should.
    [[nodiscard]] std::vector<MeteredBlock> meters() const;

Q_SIGNALS:
    /// A link the session wanted would have fed its own sound back into it,
    /// and was not made; \a path is the way round, for the error the facade
    /// reports as `routing.feedbackLoop`. **Once per ring for as long as the
    /// session runs**: the link manager asks again every time the graph
    /// moves, and a person needs telling once, not on every stream that
    /// starts.
    void feedbackRefused(const QString &path);

    /// **The audio system went away under the session** (PipeWire or
    /// WirePlumber restarting): its nodes are gone and nothing of it runs,
    /// though it has not been stopped. Once a run, after the connection has
    /// finished taking everything down.
    void audioSystemLost();

    /// **A Recorder has started a file**, at \a path. Every file started is
    /// finished, by recordingFileFinished(), whatever else happens to it.
    void recordingFileStarted(const QString &blockId, const QString &path);
    /// A Recorder's file is closed, holding \a frames at \a sampleRate.
    void recordingFileFinished(const QString &blockId, const QString &path, quint64 frames, quint32 sampleRate);
    /// A Recorder's file could not be made or written, and that Recorder has
    /// stopped writing until the session is started again or the block is
    /// switched off and on. \a reason is a record::FileError's name.
    void recordingFailed(const QString &blockId, const QString &path, const QString &reason);
    /// The disk did not keep up with a Recorder and \a seconds of audio were
    /// lost from \a path.
    void recordingOverflowed(const QString &blockId, const QString &path, double seconds);
    /// The disk a Recorder's file is on has \a bytesFree left, which is
    /// under a gigabyte. Once a file; the recording carries on until there
    /// is too little to finish it properly, and then stops with DiskFull.
    void recordingLowDiskSpace(const QString &blockId, const QString &path, quint64 bytesFree);
    /// A Recorder was paused or resumed by controlRecording(), at the
    /// frame it was asked at.
    void recordingPaused(const QString &blockId, bool paused);
    /// A Recorder ended its recording by itself: \a reason is
    /// "timeLimit" or "silence". Its file has been finished.
    void recordingStopped(const QString &blockId, const QString &reason);
    /// A Recorder's finished file could not be given its tags. The
    /// audio in it is whole.
    void recordingTagsNotWritten(const QString &blockId, const QString &path);

    /// **A plugin's host failed, and the block is linked around**:
    /// \a reason is "crashed" or "hung". Once a failure.
    void pluginFailed(const QString &blockId, const QString &reason);

    /// **The session gave way to keep its data loop alive**: its
    /// cycles were taking so long that the loop never slept, and the kernel
    /// would have killed the process. Its blocks have stopped and it is
    /// silent until the next edit or another quantum. \a heaviest are the
    /// blocks that cost the most, heaviest first, by id; empty when there
    /// was no time to measure them. Once each time it gives way.
    void overloaded(const QStringList &heaviest);

    /// **Something a person may want to hear about**: \a code is a
    /// core::kNoticeCodes name, \a blockId the block it is about, and
    /// \a subject what went or came back — a device's description, an
    /// application's name. Only while the session runs, and only for what
    /// changed under it, never for an edit.
    void noticed(const QString &code, const QString &blockId, const QString &subject);

public:
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::core
