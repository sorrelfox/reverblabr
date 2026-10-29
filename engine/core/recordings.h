// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/blockcatalogue.h"
#include "core/session.h"
#include "record/m4afile.h"
#include "record/recordingwriter.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

namespace rb::dsp
{
class Block;
}

namespace rb::core
{

/// Where a Recorder that names no folder writes: "Reverblabr" in the
/// user's music folder, which is what `xdg-user-dir MUSIC` says.
[[nodiscard]] QString defaultRecordingFolder();

/// Where the journal of recordings in progress is kept
/// (`$XDG_STATE_HOME/reverblabr/recordings`): state that has to survive a
/// crash, which is what the state directory is for.
[[nodiscard]] QString recordingJournalFolder();

/// What a file for \a block is to be made from, read off its settings now.
/// A folder that is empty is the default one, and one starting with `~/` is
/// in the home folder.
[[nodiscard]] record::RecordingSettings
recordingSettings(const dsp::Block &block, const QString &sessionName, const QString &blockName);

/// **Which of the node's input lanes a Recorder's pauses are read from**,
/// as the bits rt::AudioTap::setGapLanes() takes.
///
/// The Recorder leaves a cycle out of its file when every source feeding it
/// is an application that has stopped playing and asked for its pauses to be
/// left out (\a leavesGaps). So this is the lanes of the sources upstream of
/// \a recorderId — the blocks with no wire into them — if every one of them
/// leaves gaps, and nothing otherwise: a microphone or a Wave beside the
/// application is a sound that goes on, and its silence is silence.
/// \a inputLanes is the node's input lanes' blocks, in the order the node
/// numbers them.
[[nodiscard]] std::uint32_t recordingGapLanes(const Session &session,
                                              const QString &recorderId,
                                              const QStringList &inputLanes,
                                              const std::function<bool(const QString &blockId)> &leavesGaps);

/// **The Recorder's formats this system cannot write**, for the
/// catalogue: the M4A formats whose encoders \a probe did not find, with
/// the reason "encoderMissing". A distribution's FFmpeg may ship without
/// them, and a format offered and then refused is worse than one left out
/// with a word about how to get it.
[[nodiscard]] UnavailableChoices
recordingUnavailableChoices(const record::EncoderProbe &probe = record::probeEncoders());

/// **Repairs the recordings a crash left behind** (record::repairOrphans),
/// once, when the engine starts: a file killed in the middle of a recording
/// plays up to its last header update, and after this it plays to the end.
/// Gives back how many files were repaired.
int repairInterruptedRecordings(const QString &journalFolder);

} // namespace rb::core
