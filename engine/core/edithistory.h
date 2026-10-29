// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/editcommand.h"

#include <QString>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rb::core
{

/// One thing a person did, and how to take it back.
///
/// **A step is not a command.** Removing a block takes several commands to
/// put back — the block and every wire that went with it — and a drag of a
/// slider is a hundred commands that a person would call one thing.
struct EditStep {
    /// What puts the session back, applied in this order.
    EditInverse undo;
    /// What does it again, applied in this order.
    std::vector<EditCommand> redo;
    /// What to call it: `edit.` and the command's own name, for example
    /// `edit.addBlock`. **A code, not a sentence** — the engine has no idea
    /// what language anybody speaks, so the GUI holds the words.
    QString code;

    [[nodiscard]] bool operator==(const EditStep &other) const = default;
};

/// What one session's undo and redo stacks hold.
///
/// The history lives here, in the engine, rather than in the GUI, so that
/// the GUI and the CLI undo the same things in the same order, and so that
/// an edit that arrived over the bus from somewhere else is on the stack
/// like any other.
///
/// **Gestures, not events.** A person dragging a slider or a handful of
/// selected tiles produces a stream of edits, and Ctrl+Z should take the
/// whole drag back, not the last pixel of it. An edit that continues what
/// the one before it was doing — the same parameter of the same block, or
/// another tile in the same move — is merged into the step already there,
/// as long as it arrives within kGesture of it. Anything else starts a new
/// step, so the merging can never reach across a different kind of edit.
///
/// **Time is what a gesture is measured in**, for want of anything better:
/// nothing in the protocol says when a mouse button was released. A pause
/// longer than kGesture in the middle of a drag therefore splits it in two,
/// which is what every editor that guesses this way does.
class EditHistory
{
public:
    /// How many steps are kept. An undo stack is a convenience, not an
    /// archive; the oldest step falls off the bottom.
    static constexpr std::size_t kLimit = 200;
    /// How long after an edit a like edit still belongs to the same gesture.
    static constexpr std::chrono::milliseconds kGesture{500};

    EditHistory();

    /// Notes what \a command did, \a inverse being what applyEdit() gave
    /// back. **Anything recorded empties the redo stack**: once the history
    /// has been left, what was undone cannot be done again.
    void record(const EditCommand &command, EditInverse inverse);

    /// The same for an edit that arrived as a list of commands, \a inverse
    /// being what applyEdits() gave back for the whole of it.
    ///
    /// **A list is one step by construction**, not because of when it
    /// arrived: the caller has already said these commands are one thing —
    /// the channels of a stereo link, say — so there is nothing for the
    /// gesture window to guess at. It still joins the step before it under
    /// the same rule a single command would, so a block and the wires
    /// drawn with it are still one Ctrl+Z.
    void record(std::span<const EditCommand> commands, EditInverse inverse);

    /// Ends the gesture under way, so that the next edit starts a step of
    /// its own however soon it arrives.
    void endGesture();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    /// The code of the step undo() would take back, or an empty string.
    [[nodiscard]] QString undoCode() const;
    [[nodiscard]] QString redoCode() const;

    /// Moves the top step to the redo stack and hands it over to be
    /// applied. Nothing when there is nothing to undo.
    [[nodiscard]] std::optional<EditStep> takeUndo();
    /// The other way about.
    [[nodiscard]] std::optional<EditStep> takeRedo();

    /// Puts a step back where takeUndo() found it, for a caller that could
    /// not apply it after all.
    void putBackUndo(EditStep step);
    void putBackRedo(EditStep step);

    /// Forgets everything. A session that has been closed, or a history
    /// that could not be trusted after a failed undo, starts again empty.
    void clear();

    [[nodiscard]] std::size_t undoSize() const;
    [[nodiscard]] std::size_t redoSize() const;

    /// The clock the gesture window is measured against, in milliseconds
    /// from anywhere, as long as it only goes forwards. **A test sets its
    /// own**, so that a two-second drag costs a test no time at all.
    void setClock(std::function<std::int64_t()> clock);

private:
    /// Whether \a command carries on what \a step was doing.
    [[nodiscard]] bool continues(const EditStep &step, const EditCommand &command) const;
    void mergeInto(EditStep &step, const EditCommand &command, EditInverse inverse);

    std::deque<EditStep> m_undo;
    std::vector<EditStep> m_redo;
    std::function<std::int64_t()> m_clock;
    std::int64_t m_lastEdit = 0;
    bool m_open = false; ///< Whether a gesture is under way.
};

/// What a client is told about the stacks, and what the engine keeps so
/// that it only says it again when it has changed — a knob being dragged
/// must not send the same answer a hundred times.
struct UndoState {
    bool canUndo = false;
    QString undoCode;
    bool canRedo = false;
    QString redoCode;

    [[nodiscard]] static UndoState of(const EditHistory &history);

    [[nodiscard]] bool operator==(const UndoState &other) const = default;
};

/// The code the facade sends for a step of this command, for example
/// `edit.setParam`.
[[nodiscard]] QString editStepCode(const EditCommand &command);

} // namespace rb::core
