// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/edithistory.h"

#include <QElapsedTimer>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

/// The monotonic clock the gesture window is measured against by default.
/// Monotonic, because the wall clock can step backwards and would then merge
/// a gesture that ended minutes ago.
[[nodiscard]] std::int64_t sinceTheProcessStarted()
{
    static const QElapsedTimer running = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return running.elapsed();
}

/// The block a move command is about, or nothing when the command is not a
/// move.
[[nodiscard]] std::optional<QString> movedBlock(const EditCommand &command)
{
    if (const auto *move = std::get_if<MoveBlock>(&command)) {
        return move->blockId;
    }
    return std::nullopt;
}

/// Whether two wires meet at the same place: the same output port, or the
/// same input port. **What tells a wire that moved from two wires that were
/// drawn**, which is the difference between one Ctrl+Z and two.
[[nodiscard]] bool sharesAnEnd(const Wire &one, const Wire &other)
{
    return (one.fromBlock == other.fromBlock && one.fromPort == other.fromPort)
        || (one.toBlock == other.toBlock && one.toPort == other.toPort);
}

} // namespace

UndoState UndoState::of(const EditHistory &history)
{
    return UndoState{history.canUndo(), history.undoCode(), history.canRedo(), history.redoCode()};
}

QString editStepCode(const EditCommand &command)
{
    const std::string_view name = editCommandName(command);
    return "edit."_L1 + QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

EditHistory::EditHistory()
    : m_clock(&sinceTheProcessStarted)
{
}

void EditHistory::setClock(std::function<std::int64_t()> clock)
{
    m_clock = clock ? std::move(clock) : std::function<std::int64_t()>(&sinceTheProcessStarted);
}

void EditHistory::endGesture()
{
    m_open = false;
}

bool EditHistory::continues(const EditStep &step, const EditCommand &command) const
{
    if (!m_open || step.redo.empty()) {
        return false;
    }
    if (m_clock() - m_lastEdit > kGesture.count()) {
        return false; // the hand stopped; whatever comes now is a new thing
    }
    const EditCommand &last = step.redo.back();

    // The same knob being turned again.
    if (const auto *now = std::get_if<SetParam>(&command)) {
        const auto *before = std::get_if<SetParam>(&last);
        return before != nullptr && before->blockId == now->blockId && before->key == now->key;
    }
    // A wire the session drew by itself, as part of what the person really
    // did. Automatic mode wires a new block to the one it was added after,
    // and closes the gap a removed block leaves; taking back half of that
    // would leave a session nobody ever saw — a block with no wire to it,
    // or a lane with a hole in the middle. **The step is asked what started
    // it**, not what it last did, because closing a gap can take more than
    // one wire.
    if (const auto *now = std::get_if<Connect>(&command)) {
        const EditCommand &started = step.redo.front();
        if (std::holds_alternative<AddBlock>(started) || std::holds_alternative<RemoveBlock>(started)) {
            return true;
        }
        // A wire dragged off one port and onto another: the disconnect and
        // the connect are one gesture, and they are told apart from two
        // wires edited in quick succession by the end they have in common.
        const auto *before = std::get_if<Disconnect>(&last);
        return before != nullptr && sharesAnEnd(before->wire, now->wire);
    }
    // Another tile of the same move, or the same tile moved further.
    return movedBlock(command).has_value() && movedBlock(last).has_value();
}

void EditHistory::mergeInto(EditStep &step, const EditCommand &command, EditInverse inverse)
{
    // **The first inverse of the gesture is the one that matters**: it holds
    // where the tile started and what the knob read before the drag. What is
    // replaced is the redo side, which has to end up where the gesture did.
    if (const std::optional<QString> block = movedBlock(command)) {
        const auto sameBlock = [&block](const EditCommand &existing) {
            const std::optional<QString> other = movedBlock(existing);
            return other.has_value() && *other == *block;
        };
        const auto found = std::ranges::find_if(step.redo, sameBlock);
        if (found != step.redo.end()) {
            *found = command; // this tile has moved in this gesture already
            return;
        }
        // A tile that joins the gesture brings its own starting point. The
        // undo side is applied in order, so the newcomer's inverse goes
        // where the others' did.
        step.undo.insert(step.undo.end(), inverse.begin(), inverse.end());
        step.redo.push_back(command);
        return;
    }
    if (std::holds_alternative<Connect>(command)) {
        // **The front of the undo side, not the end.** These commands depend
        // on each other — the wire is drawn to a block the same step added —
        // so putting the session back means taking the last one back first.
        // Appending would undo the block before the wire it carries, and the
        // wire's own undo would then have nothing to disconnect.
        step.undo.insert(step.undo.begin(), inverse.begin(), inverse.end());
        step.redo.push_back(command);
        return;
    }
    step.redo.back() = command;
}

void EditHistory::record(const EditCommand &command, EditInverse inverse)
{
    m_redo.clear();

    if (!m_undo.empty() && continues(m_undo.back(), command)) {
        mergeInto(m_undo.back(), command, std::move(inverse));
        m_lastEdit = m_clock();
        return;
    }

    EditStep step;
    step.undo = std::move(inverse);
    step.redo.push_back(command);
    step.code = editStepCode(command);
    m_undo.push_back(std::move(step));
    while (m_undo.size() > kLimit) {
        m_undo.pop_front();
    }
    m_lastEdit = m_clock();
    m_open = true;
}

void EditHistory::record(std::span<const EditCommand> commands, EditInverse inverse)
{
    if (commands.empty()) {
        return;
    }
    m_redo.clear();

    // **Asked about the first, and then the whole list goes in.** Whether a
    // list joins what came before is the same question as for one command —
    // the wires drawn with a new block belong to the block — and the answer
    // cannot differ between the channels of one link.
    if (!m_undo.empty() && continues(m_undo.back(), commands.front())) {
        EditStep &step = m_undo.back();
        // The list's own inverse already undoes the list; it goes in front
        // of what is there, because a step is taken back from its end.
        step.undo.insert(step.undo.begin(), inverse.begin(), inverse.end());
        step.redo.insert(step.redo.end(), commands.begin(), commands.end());
        m_lastEdit = m_clock();
        return;
    }

    EditStep step;
    step.undo = std::move(inverse);
    step.redo.assign(commands.begin(), commands.end());
    step.code = editStepCode(commands.front());
    m_undo.push_back(std::move(step));
    while (m_undo.size() > kLimit) {
        m_undo.pop_front();
    }
    m_lastEdit = m_clock();
    m_open = true;
}

bool EditHistory::canUndo() const
{
    return !m_undo.empty();
}

bool EditHistory::canRedo() const
{
    return !m_redo.empty();
}

QString EditHistory::undoCode() const
{
    return m_undo.empty() ? QString{} : m_undo.back().code;
}

QString EditHistory::redoCode() const
{
    return m_redo.empty() ? QString{} : m_redo.back().code;
}

std::optional<EditStep> EditHistory::takeUndo()
{
    if (m_undo.empty()) {
        return std::nullopt;
    }
    EditStep step = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back(step);
    // Undoing ends any gesture: the next edit is a thing of its own.
    m_open = false;
    return step;
}

std::optional<EditStep> EditHistory::takeRedo()
{
    if (m_redo.empty()) {
        return std::nullopt;
    }
    EditStep step = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back(step);
    m_open = false;
    return step;
}

void EditHistory::putBackUndo(EditStep step)
{
    if (!m_redo.empty()) {
        m_redo.pop_back();
    }
    m_undo.push_back(std::move(step));
    m_open = false;
}

void EditHistory::putBackRedo(EditStep step)
{
    if (!m_undo.empty()) {
        m_undo.pop_back();
    }
    m_redo.push_back(std::move(step));
    m_open = false;
}

void EditHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
    m_open = false;
}

std::size_t EditHistory::undoSize() const
{
    return m_undo.size();
}

std::size_t EditHistory::redoSize() const
{
    return m_redo.size();
}

} // namespace rb::core
