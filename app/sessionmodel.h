// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QObject>
#include <QPointF>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// The session the window is showing, as the interface reads it.
///
/// **The engine's copy is the real one.** This follows it: a snapshot when
/// the session opens, and every edit after that as a numbered patch, applied
/// with the engine's own `applyEdit` so that the two cannot drift. A gap in
/// the numbering is something to notice rather than something to paper over,
/// so it asks for the snapshot again.
///
/// **An edit goes to the engine and comes back as a patch**; the panel never
/// writes into its own copy and then hopes. That is what makes undo, a
/// second window and a daemon all behave the same.
///
/// **A turned knob is the exception, deliberately.** The engine sends no
/// patch for `SetParam` — sixty a second to tell the client what it just
/// did would be sixty messages a second saying nothing — so the value is
/// written here as it is sent. An undo *does* arrive as a patch, and
/// overwrites it, which is how the two stay in step.
///
/// A knob being dragged is also the one thing not sent as it happens: sixty
/// values a second is what a slider produces and what a smoothed parameter
/// can take, but six hundred is what a mouse can produce.
class SessionModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The session being followed, or empty. Set by the window.
    Q_PROPERTY(QString sessionId READ sessionId WRITE setSessionId NOTIFY sessionChanged)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    /// What the person wrote about this session.
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY changed)
    /// Whether the session wires new blocks up as they arrive.
    Q_PROPERTY(bool autoConnect READ autoConnect NOTIFY changed)
    /// "auto", or a rate in hertz written out. **A code, not a word to
    /// read**; the interface holds the words.
    Q_PROPERTY(QString sampleRateSetting READ sampleRateSetting NOTIFY changed)
    /// "default", "low" or "high", in that order of how little delay the
    /// session asks the audio system for.
    Q_PROPERTY(QString latency READ latency NOTIFY changed)
    /// "off", or "login" for a session the background engine starts once a
    /// login, and again after a crash (ADR-0023).
    Q_PROPERTY(QString autoRun READ autoRun NOTIFY changed)
    /// The session's timers this version can run, in the order the session
    /// holds them, each as the file holds it (core/timers.h) with `nextRun`,
    /// a date, or undefined when it will not start the session again.
    Q_PROPERTY(QVariantList timers READ timers NOTIFY changed)
    /// Every block in the session, in the order it holds them.
    Q_PROPERTY(QStringList blockIds READ blockIds NOTIFY changed)
    /// How many wires there are, so that something can bind to "is this
    /// session wired up at all" without reading the list to count it.
    Q_PROPERTY(int wireCount READ wireCount NOTIFY changed)
    /// The patch number last applied, so a test can see that an edit landed.
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    /// The same number, for **what a binding drawing a block's parameters
    /// reads**: a curve, a tile's face, a readout. It is said to change
    /// whenever revision is, just before it, and also each time a value
    /// being dragged has been sent, at most once a kLiveIntervalMs, so what
    /// is drawn from params() moves with the slider rather than waiting for
    /// it to be let go. revision and changed() stay quiet until the gesture
    /// ends: what follows those rebuilds tiles, wires and lists, and doing
    /// that sixty times a second for a slider cost a fifty-tile canvas half
    /// as much again as drawing it.
    Q_PROPERTY(qulonglong paramRevision READ revision NOTIFY paramRevisionChanged)
    /// Whether the session showing has anything to undo or redo, and what
    /// the menu entry says: "Undo Move Block", or plain "Undo" when there
    /// is nothing. **The engine's history, not one kept here**: it
    /// says what changed, and this only repeats it for the session showing.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)
    /// Counts up whenever what is known of a plugin block's own window or
    /// presets changes: **what a binding reads** to follow
    /// pluginWindowOpen() and the rest.
    Q_PROPERTY(int pluginRevision READ pluginRevision NOTIFY pluginChanged)

public:
    /// How often a parameter being dragged is sent. Sixty a second is what
    /// a slider produces and what a smoothed parameter can take; a mouse
    /// can produce ten times that, and every one of them would be an edit.
    static constexpr int kLiveIntervalMs = 16;
    /// How often a drag whose hand has stopped is sent again, well inside
    /// the half second within which the engine counts an edit as part of
    /// the step before it.
    static constexpr int kHoldIntervalMs = 250;
    /// How long a drag is held with no movement at all before it is let go
    /// of as abandoned: the longest rest the engine allows a gesture a
    /// plugin window says is under way.
    static constexpr int kHoldLimitMs = 30000;

    explicit SessionModel(QObject *parent = nullptr);
    ~SessionModel() override;

    static SessionModel *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    void attach(core::EngineFacade *facade);

    [[nodiscard]] QString sessionId() const;
    void setSessionId(const QString &sessionId);
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString notes() const;
    [[nodiscard]] bool autoConnect() const;
    [[nodiscard]] QString sampleRateSetting() const;
    [[nodiscard]] QString latency() const;
    [[nodiscard]] QString autoRun() const;
    [[nodiscard]] QVariantList timers() const;
    [[nodiscard]] QStringList blockIds() const;
    [[nodiscard]] int wireCount() const;
    [[nodiscard]] qulonglong revision() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] QString redoText() const;

    /// Takes back the last gesture in the session showing, or puts back the
    /// last one taken back. What changes arrives as patches, like any edit.
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    /// Starts a plugin block's plugin again after its host failed.
    /// Not an edit: nothing about the session changes, so nothing to undo.
    Q_INVOKABLE void restartPlugin(const QString &blockId);

    [[nodiscard]] int pluginRevision() const;
    /// **A plugin block's own window**, which its host shows. Not an edit;
    /// what it is now arrives from the engine, and pluginWindowOpen() says
    /// it, with why it is not showing when it is not.
    Q_INVOKABLE void setPluginWindow(const QString &blockId, bool open);
    [[nodiscard]] Q_INVOKABLE bool pluginWindowOpen(const QString &blockId) const;
    /// "closed", "unavailable", "failed", "crashed", "notRunning", or empty
    /// when it has never been asked for.
    [[nodiscard]] Q_INVOKABLE QString pluginWindowReason(const QString &blockId) const;
    /// **A plugin block's own presets**, its format's: asked for, then
    /// read as `{id, name}` each once the engine has answered. Putting one
    /// in is an edit, which undo takes back; saving one writes it where
    /// the format's other hosts find it.
    Q_INVOKABLE void listPluginPresets(const QString &blockId);
    [[nodiscard]] Q_INVOKABLE QVariantList pluginPresets(const QString &blockId) const;
    Q_INVOKABLE void loadPluginPreset(const QString &blockId, const QString &presetId);
    Q_INVOKABLE void savePluginPreset(const QString &blockId, const QString &name);
    /// The plugin's own preset its settings are, or empty for none.
    [[nodiscard]] Q_INVOKABLE QString pluginPresetShown(const QString &blockId) const;

    /// What a block is: its type id, what it is called, whether it is on,
    /// and where its tile sits.
    [[nodiscard]] Q_INVOKABLE QVariantMap block(const QString &blockId) const;
    /// One field of it each. **For the canvas's per-frame arithmetic**: a
    /// wire's two ends are worked out again on every frame of a drag, and
    /// building the whole of block() to read one field of it is a map a
    /// frame per end. Empty and a null point for a block that is not there.
    [[nodiscard]] Q_INVOKABLE QString blockType(const QString &blockId) const;
    [[nodiscard]] Q_INVOKABLE QPointF blockPosition(const QString &blockId) const;
    /// One block's parameters as they stand — **only those that were set**.
    /// A parameter the session does not mention is at its default, which is
    /// the descriptor's business and not the session's.
    [[nodiscard]] Q_INVOKABLE QVariantMap params(const QString &blockId) const;
    /// One parameter's value, or an invalid variant when the session does
    /// not mention it.
    [[nodiscard]] Q_INVOKABLE QVariant param(const QString &blockId, const QString &paramId) const;

    /// Sets a parameter. \a live is true while a control is being dragged:
    /// such values are coalesced and sent at kLiveIntervalMs, and the last
    /// one is always sent. With \a live false the value goes at once, which
    /// is what letting go of a slider does.
    ///
    /// **A live value holds the drag open** until commitParam(): a hand
    /// that stops moving has its value sent again every kHoldIntervalMs,
    /// so the drag stays one step to undo however long it rests. An undo,
    /// a value set outright, another session, the window losing focus or
    /// kHoldLimitMs without a movement each let go of it.
    Q_INVOKABLE void setParam(const QString &blockId, const QString &paramId, const QVariant &value, bool live = false);
    /// Sends whatever a drag has left pending, and says the gesture is over
    /// whether or not anything was. **What makes the last value of a drag
    /// always arrive**, whatever the timer was doing, and what lets the
    /// window see it.
    Q_INVOKABLE void commitParam(const QString &blockId, const QString &paramId);
    /// Sets several of \a blockId's parameters **as one edit, in order**:
    /// \a changes is a list of `[paramId, value]` pairs. One step to undo,
    /// and the block hears them in the order given — which is how a Mixer
    /// fade is sent, as the ramp, the volume and the ramp put back.
    Q_INVOKABLE void setParams(const QString &blockId, const QVariantList &changes);
    /// **Fit from audiogram**: \a blockId's band settings worked
    /// out from its own audiogram, for its own bands, and sent as one edit
    /// that one undo takes back. What the fit said about each band, low to
    /// high: `{band, noResponse, beyondMaximum}` — the bands where more gain
    /// is unlikely to help, which the panel marks. Empty for a block with
    /// nothing to fit.
    Q_INVOKABLE QVariantList fitFromAudiogram(const QString &blockId);

    /// What the session is called. Empty is refused: a session with no name
    /// is a session nobody can find again.
    Q_INVOKABLE void renameSession(const QString &name);
    /// What the person wrote about the session. **Empty is sent**, unlike a
    /// name: emptying the box is how somebody takes a note back, and there
    /// is nothing about a note that can be wrong.
    Q_INVOKABLE void setNotes(const QString &notes);
    /// The session's own settings. Each goes to the engine as an edit and
    /// comes back as a patch, so undo takes it back like anything else.
    Q_INVOKABLE void setAutoConnect(bool autoConnect);
    /// "auto", or a rate in hertz written out.
    Q_INVOKABLE void setSampleRateSetting(const QString &sampleRate);
    /// "default", "low" or "high".
    Q_INVOKABLE void setLatency(const QString &latency);
    /// "off" or "login".
    Q_INVOKABLE void setAutoRun(const QString &autoRun);

    /// A timer to start editing from: a new id, on, every day at the next
    /// whole hour, for an hour. Nothing is sent until setTimer().
    [[nodiscard]] Q_INVOKABLE QVariantMap newTimer() const;
    /// Whether \a timer is one the session would take: every field it
    /// needs, nothing wrong with any, and for a one-off a time still to come.
    [[nodiscard]] Q_INVOKABLE bool timerIsValid(const QVariantMap &timer) const;
    /// The systemd calendar expression \a timer starts on, for a person
    /// who wants to see what is scheduled; empty for a timer that is not
    /// valid.
    [[nodiscard]] Q_INVOKABLE QString timerCalendar(const QVariantMap &timer) const;
    /// Adds \a timer, or replaces the one with its id, as one edit. The
    /// background engine makes its systemd timer.
    Q_INVOKABLE void setTimer(const QVariantMap &timer);
    /// Switches a timer on or off, keeping everything else about it.
    Q_INVOKABLE void setTimerEnabled(const QString &timerId, bool enabled);
    Q_INVOKABLE void removeTimer(const QString &timerId);

    /// What the user calls a block. Empty means the block's own name.
    Q_INVOKABLE void renameBlock(const QString &blockId, const QString &nickname);
    /// On or off — bypass, which the graph cross-fades.
    Q_INVOKABLE void setBlockEnabled(const QString &blockId, bool enabled);

    /// Puts a block of \a typeId on the canvas at \a position and answers
    /// with the id it was given, or an empty string if there was nowhere to
    /// put it. **The id is chosen here**, from the copy of the session this
    /// model already follows, because the caller needs to know at once
    /// which tile to select; an id already in use is refused by the engine
    /// like any other bad edit, and nothing is written here until the patch
    /// comes back.
    Q_INVOKABLE QString addBlock(const QString &typeId, const QPointF &position);
    /// Takes a block off the canvas, with every wire that went with it.
    ///
    /// **With `autoConnect` on it closes the gap behind it**: what fed the
    /// block's input n is wired to what its output n fed, so that taking
    /// one effect out of a chain leaves a chain rather than two halves and
    /// silence. Channel to channel, not everything to everything, or
    /// removing a stereo block would leave a mono sum of both channels
    /// where two channels had been. The engine's history merges those wires
    /// into the removal, so it is one Ctrl+Z.
    Q_INVOKABLE void removeBlock(const QString &blockId);
    /// The same for several at once, which is what Del on a multiple
    /// selection does. **One call rather than a loop in QML**, so that the
    /// ids are all read before the first removal changes what is left.
    Q_INVOKABLE void removeBlocks(const QStringList &blockIds);
    /// Where a tile sits. Sent when a drag ends rather than while it runs:
    /// what the engine does with a position is write it down, and sixty
    /// edits a second to say where a tile is halfway to would be sixty
    /// edits a second nobody asked for.
    Q_INVOKABLE void moveBlock(const QString &blockId, const QPointF &position);

    // --- wires ---
    //
    // **The engine's half was already there**: `Connect` and `Disconnect`
    // are edit commands like any other, and a session file has held its
    // wires since it could hold blocks. What was missing was anywhere for
    // the interface to read them or ask for one, which is all of this.
    //
    // **A wire is a value, not an object.** There is no wire id anywhere:
    // a wire *is* its two ends, which is how the session file spells it and
    // how the engine compares them. So everything here takes four
    // arguments, and QML holds a wire as the map wires() gives back.

    /// Every wire in the session, in the order it holds them. Each one is
    /// `{fromBlock, fromPort, toBlock, toPort}`.
    [[nodiscard]] Q_INVOKABLE QVariantList wires() const;
    /// Every wire with \a blockId at either end, which is what a tile needs
    /// to be able to say what it is connected to.
    [[nodiscard]] Q_INVOKABLE QVariantList wiresTouching(const QString &blockId) const;
    /// How many wires arrive at one input port. **What draws the merge
    /// marker**: more than one wire into a port is an implicit sum in the
    /// engine, and a person who cannot see that a port sums cannot tell a
    /// mistake from a mix.
    [[nodiscard]] Q_INVOKABLE int arrivingCount(const QString &blockId, int port) const;
    /// The highest port of \a blockId any wire uses, plus one, or nought
    /// when nothing is wired to that side of it. **What a tile grows to**:
    /// a block whose shape follows what is connected to it has as many
    /// ports as the highest wire asks for, which is what the graph compiler
    /// gives its node.
    [[nodiscard]] Q_INVOKABLE int portsInUse(const QString &blockId, bool output) const;
    [[nodiscard]] Q_INVOKABLE bool
    hasWire(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort) const;

    /// Why this wire would be refused, as the engine's own name for it —
    /// `wireWouldCycle`, `wireAlreadyExists` and so on — or empty when it
    /// would be taken. **Asked while the pointer is still moving**, so that
    /// a port nothing can be dropped on never lights up, and answered from
    /// the same rules the engine applies rather than from a second copy of
    /// them.
    ///
    /// It knows nothing about how many ports a block has: that is the block
    /// registry's, which reaches the interface as the catalogue, so the
    /// canvas asks that separately.
    [[nodiscard]] Q_INVOKABLE QString wireRefusal(const QString &fromBlock,
                                                  int fromPort,
                                                  const QString &toBlock,
                                                  int toPort) const;

    /// Draws a wire, if the session will take it. Answers whether it was
    /// sent — worked out from this model's own copy of the session, which
    /// the engine's applyEdit() keeps in step, so the answer is the
    /// engine's answer and the caller does not have to wait for a patch to
    /// find out.
    /// How many links arrive at a block's input lane. **What draws the
    /// merge marker**: two links into one lane are an implicit sum in the
    /// engine, and a person who cannot see that a lane sums cannot tell a
    /// mistake from a mix. Counted in links and not in wires, because the
    /// channels of one link are one arrival and not two.
    ///
    /// **One lane of several**: with \a firstPort at nought or above,
    /// only the links arriving at ports \a firstPort to \a firstPort +
    /// \a channels - 1 are counted — the ducker's background lane, say, and
    /// not its main one. The canvas knows how wide a lane is, from the
    /// catalogue; this model does not.
    [[nodiscard]] Q_INVOKABLE int arrivingLinks(const QString &blockId, int firstPort = -1, int channels = 0) const;

    /// Draws a link: one wire a channel, from \a fromBlock's output lane to
    /// \a toBlock's input lane, for as many channels as both have.
    ///
    /// **A link is one edit.** The engine takes the whole list in one call,
    /// so it applies whole or not at all and it is one step to take back —
    /// what a person drew was a link, and half a link is not something they
    /// can have meant. Answers whether it was sent.
    ///
    /// **Into which lane** is \a toFirstPort, the first port of it: nought
    /// for a block with one lane, and for the ducker's background lane two.
    Q_INVOKABLE bool connectLanes(const QString &fromBlock, const QString &toBlock, int channels, int toFirstPort = 0);
    /// Takes the whole link out, every channel of it, as one edit. With
    /// \a toFirstPort at nought or above, only the link into that lane of
    /// \a channels ports: a source wired to both of a ducker's lanes is two
    /// links, and a person took out one.
    Q_INVOKABLE void
    disconnectLanes(const QString &fromBlock, const QString &toBlock, int toFirstPort = -1, int channels = 0);
    /// Moves a link from one block's input lane to another's, as one edit.
    /// Answers whether the new link was taken; the old one stays where it
    /// was if it was not. The lanes are given by their first ports and
    /// widths as above; \a wasFirstPort below nought is every link between
    /// the two.
    Q_INVOKABLE bool moveLane(const QString &fromBlock,
                              const QString &wasToBlock,
                              const QString &toBlock,
                              int channels,
                              int wasFirstPort = -1,
                              int wasChannels = 0,
                              int toFirstPort = 0);

    Q_INVOKABLE bool connectBlocks(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort);
    Q_INVOKABLE void disconnectBlocks(const QString &fromBlock, int fromPort, const QString &toBlock, int toPort);

    /// How many `SetParam` calls have been sent to the engine. **For a test**
    /// measuring what a drag costs.
    [[nodiscard]] int sentParamCount() const;

Q_SIGNALS:
    void sessionChanged();
    void changed();
    /// Said just before changed(), and on its own when a dragged value has
    /// been sent (see paramRevision).
    void paramRevisionChanged();
    void undoStateChanged();
    void pluginChanged();

private:
    /// Says changed(), and paramRevisionChanged() before it.
    void announce();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
