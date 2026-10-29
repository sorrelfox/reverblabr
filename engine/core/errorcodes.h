// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace rb::core
{

/// Every error the engine can report. The engine never produces user-visible
/// text: it reports {code, args}, and the GUI translates the code (see
/// app/errormessages.cpp, where a unit test requires a message for each code).
///
/// To add an error, add an enumerator here and a row to kErrorCodes, then a
/// message to the GUI's table. Released codes never change their name.
enum class ErrorCode {
    BlockUnknownType,
    GraphCycle,
    RoutingFeedbackLoop,
    SessionCorruptFile,
    SessionNotFound,
    SessionNotSaved,
    SessionCannotRun,
    EditRefused,
    PresetRefused,
    VirtualDeviceNotCreated,
    VirtualDeviceRefused,
    VirtualDeviceNodeRefused,
    RecordingFailed,
    RecordingOverflow,
    RecordingLowDiskSpace,
    RecordingRefused,
    RecordingTagsNotWritten,
    PluginCrashed,
    PluginStoppedResponding,
    PluginRestartRefused,
    RecordingPreviewFailed,
    RecordingNotFound,
    RecordingRenameRefused,
    RecordingTagsNotSaved,
    SessionOverloaded,
    DaemonUnreachable,
    TimerNotFound,
    Count, ///< Not an error; the number of codes.
};

/// How an error code travels over the facade: a stable name of the form
/// "<area>.<camelCaseName>", and the names of its arguments in the order the
/// GUI message uses them (%1, %2, ...).
struct ErrorCodeInfo {
    ErrorCode code;
    std::string_view name;
    std::array<std::string_view, 3> arguments{};
};

inline constexpr std::array kErrorCodes{
    // A session contains a block type this version doesn't know. The block is kept.
    ErrorCodeInfo{ErrorCode::BlockUnknownType, "block.unknownType", {"type"}},
    // The wires of a session form a cycle, so it can't be compiled.
    ErrorCodeInfo{ErrorCode::GraphCycle, "graph.cycle"},
    // A link would feed a session's output back into its own input, so it was
    // refused. The argument is the way round the loop, node by node.
    ErrorCodeInfo{ErrorCode::RoutingFeedbackLoop, "routing.feedbackLoop", {"path"}},
    // A session file could not be read; it was moved aside as a backup.
    ErrorCodeInfo{ErrorCode::SessionCorruptFile, "session.corruptFile", {"file", "backup"}},
    // There is no session of that id to open.
    ErrorCodeInfo{ErrorCode::SessionNotFound, "session.notFound", {"session"}},
    // A session could not be written to disk. Nothing was lost; it is still open.
    ErrorCodeInfo{ErrorCode::SessionNotSaved, "session.notSaved", {"session", "reason"}},
    // A session could not be started: a block it names is missing, or the
    // graph will not compile, or PipeWire would not take the node.
    ErrorCodeInfo{ErrorCode::SessionCannotRun, "session.cannotRun", {"session", "reason"}},
    // An edit was refused and the session is exactly as it was.
    ErrorCodeInfo{ErrorCode::EditRefused, "edit.refused", {"reason"}},
    // A preset could not be applied, saved, renamed or deleted. Nothing was
    // changed: not the block, and not what is on disk.
    ErrorCodeInfo{ErrorCode::PresetRefused, "preset.refused", {"preset", "reason"}},
    // A virtual device could not be made (ADR-0013). Nothing was written.
    // The reason is a pw::VirtualDeviceError's name.
    ErrorCodeInfo{ErrorCode::VirtualDeviceNotCreated, "device.virtualNotCreated", {"name", "reason"}},
    // A virtual device could not be renamed, switched on or off, or deleted,
    // and is exactly as it was. The reason is a pw::VirtualDeviceError's
    // name; ReadOnlyScope means an all-users device, which only the
    // privileged helper may change.
    ErrorCodeInfo{ErrorCode::VirtualDeviceRefused, "device.virtualRefused", {"device", "reason"}},
    // PipeWire would not make a virtual device's node. The device is kept in
    // its file, and the next reconcile tries again.
    ErrorCodeInfo{ErrorCode::VirtualDeviceNodeRefused, "device.virtualNodeRefused", {"device"}},
    // A Recorder could not make or write its file, and has stopped writing
    // until the session is started again or the block is switched off and
    // on. Whatever it had written is kept and closed. The reason is a
    // record::FileError's name. The error names the block (argument
    // "block") so it is shown on its tile.
    ErrorCodeInfo{ErrorCode::RecordingFailed, "recording.failed", {"file", "reason"}},
    // The disk did not keep up with a Recorder and some seconds of audio are
    // missing from its file. The recording carries on.
    ErrorCodeInfo{ErrorCode::RecordingOverflow, "recording.overflow", {"file", "seconds"}},
    // A warning, not a failure: the disk a Recorder's file is on has
    // under a gigabyte left, given in whole megabytes. The recording carries
    // on, and stops with recording.failed and DiskFull before it is full.
    ErrorCodeInfo{ErrorCode::RecordingLowDiskSpace, "recording.lowDiskSpace", {"file", "megabytes"}},
    // A Recorder could not be paused, resumed or split: the session is
    // not running, there is no such block, it is not a Recorder, or there is
    // no such action. Nothing changed. Names the block.
    ErrorCodeInfo{ErrorCode::RecordingRefused, "recording.refused", {"reason"}},
    // A finished recording's tags could not be written: the file is
    // whole and plays, without its title, artist and the rest. Names the
    // block.
    ErrorCodeInfo{ErrorCode::RecordingTagsNotWritten, "recording.tagsNotWritten", {"file"}},
    // A plugin's host process went. The block is linked around, so
    // the session runs on without the plugin, and it can be restarted. The
    // argument is the plugin's id. Names the block.
    ErrorCodeInfo{ErrorCode::PluginCrashed, "plugin.crashed", {"plugin"}},
    // A plugin's host stopped running and was killed; the same as
    // plugin.crashed otherwise. Names the block.
    ErrorCodeInfo{ErrorCode::PluginStoppedResponding, "plugin.stoppedResponding", {"plugin"}},
    // A plugin could not be restarted: the session is not running, there is
    // no such block, or it is not a plugin that runs in a host. Names the
    // block.
    ErrorCodeInfo{ErrorCode::PluginRestartRefused, "plugin.restartRefused", {"reason"}},
    // A recording could not be listened to: the file cannot be read
    // ("unreadable"), there is no audio system ("noAudioSystem"), or the
    // server took the preview's stream down (what it said). Nothing else is
    // affected; the preview is stopped.
    ErrorCodeInfo{ErrorCode::RecordingPreviewFailed, "recording.previewFailed", {"file", "reason"}},
    // The Recording Inspector asked about a file the recordings index does
    // not have: it was deleted, moved, or never recorded here.
    ErrorCodeInfo{ErrorCode::RecordingNotFound, "recording.notFound", {"file"}},
    // A recording was not renamed: the name is not a file name
    // ("invalidName"), another file has it ("exists"), the file has gone
    // ("notFound"), or the disk refused ("failed"). Nothing changed.
    ErrorCodeInfo{ErrorCode::RecordingRenameRefused, "recording.renameRefused", {"file", "reason"}},
    // A recording's tags could not be written: the file is as it was.
    ErrorCodeInfo{ErrorCode::RecordingTagsNotSaved, "recording.tagsNotSaved", {"file"}},
    // A session's cycles took so long that its audio thread never rested,
    // and the kernel would have ended the whole program. It has
    // stopped running its blocks and is silent until it is changed or the
    // quantum is. Names the block that cost the most, when it could be
    // measured.
    ErrorCodeInfo{ErrorCode::SessionOverloaded, "session.overloaded", {"session"}},
    // **Reported by the window, not by an engine**: the engine in
    // reverblabrd could not be reached, so this window runs its own, and its
    // sessions stop when it closes. The reason is "noSessionBus",
    // "daemonNotInstalled" (no activation file names it: a build that has not
    // been installed) or "daemonRefused" (the bus tried, and it did not start).
    ErrorCodeInfo{ErrorCode::DaemonUnreachable, "engine.daemonUnreachable", {"reason"}},
    // FireTimer named a timer no session has: it was deleted, or its session
    // was. Nothing was started.
    ErrorCodeInfo{ErrorCode::TimerNotFound, "timer.notFound", {"timer"}},
};

static_assert(kErrorCodes.size() == static_cast<std::size_t>(ErrorCode::Count),
              "every ErrorCode needs exactly one row in kErrorCodes");
static_assert(
    [] {
        for (std::size_t i = 0; i < kErrorCodes.size(); ++i) {
            if (kErrorCodes[i].code != static_cast<ErrorCode>(i)) {
                return false;
            }
        }
        return true;
    }(),
    "kErrorCodes rows must be in ErrorCode order");

/// The stable name of an error code, for example "graph.cycle".
[[nodiscard]] constexpr std::string_view errorCodeName(ErrorCode code) noexcept
{
    return kErrorCodes[static_cast<std::size_t>(code)].name;
}

} // namespace rb::core
