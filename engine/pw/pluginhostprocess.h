// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace rb::pw
{

/// The reverblabr-plugin-host executable: `REVERBLABR_PLUGIN_HOST` when it
/// is set, the installed one when it is there, and the one in the build tree
/// otherwise.
[[nodiscard]] QString defaultPluginHostProgram();

/// Which plugin a host is to run, and where the host is.
struct PluginHostSpec {
    /// The reverblabr-plugin-host executable.
    QString program = defaultPluginHostProgram();
    /// "builtin" until the plugin formats arrive.
    QString format = QStringLiteral("builtin");
    QString plugin;
    /// Names the host's node: `reverblabr.plugin.<instance>`.
    QString instance;
    /// For the built-in test plugin: how much it delays, and reports.
    std::uint32_t latencySamples = 0;
    /// What a plugin with one input does on a stereo lane: "dualMono" or
    /// "downmix".
    QString monoMode = QStringLiteral("dualMono");
    /// Where the plugin was found, as the scan says: what a host is told to
    /// load for a format whose id does not say where the plugin is — a
    /// CLAP id does not.
    QString location;

    [[nodiscard]] bool operator==(const PluginHostSpec &other) const = default;
};

/// **One plugin host, from the engine's side** (ADR-0014): the process, the
/// control socket and the shared memory, and the watch kept on all three.
///
/// It starts `reverblabr-plugin-host` with a socketpair and a memfd on file
/// descriptors 3 and 4, and hears back `ready()` once the host's node is in
/// the graph, with its name — which is what the session links — and the
/// plugin's latency.
///
/// **A host that goes is noticed at once, and one that stops within a few
/// cycles.** The process is watched through a pidfd, so its exit arrives as
/// an event the moment the kernel reaps it, with no polling. A host that is
/// still there but no longer running — a plugin in an endless loop, or a
/// process stopped — is what watch() is for: its heartbeat standing still
/// while the audio feeding it goes on. Either way the session bypasses the
/// block by linking around it; ADR-0014 measured that at about 10 ms.
/// Noticed at 0.4 ms for an exit and 45 ms for a stop (`pw.plugin-host`).
///
/// Lives on the engine thread.
class PluginHostProcess : public QObject
{
    Q_OBJECT

public:
    explicit PluginHostProcess(QObject *parent = nullptr);
    /// Kills a host that is still there: its node goes with it.
    ~PluginHostProcess() override;

    /// Starts the host. False if it could not even be started; everything
    /// after that arrives as signals.
    [[nodiscard]] bool start(const PluginHostSpec &spec);

    /// Asks the host to finish, and kills it if it has not within \a grace.
    void stop(std::chrono::milliseconds grace = std::chrono::milliseconds{500});

    /// Kills the host now, with nothing asked first: for one that has
    /// stopped running and would never answer. Its exit arrives as ever.
    void kill();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] int pid() const;
    /// The host's node, once ready().
    [[nodiscard]] QString nodeName() const;
    [[nodiscard]] std::uint32_t latencySamples() const;

    /// **A parameter, to the host's audio thread**, through the shared
    /// ring: heard from the next cycle, with nothing allocated or sent
    /// through a socket. False when the ring is full.
    ///
    /// **While a state loads it is held back** and sent once the host says
    /// the plugin with the state in it is running (stateLoaded()): through
    /// the ring it would reach the plugin being replaced, and be lost with
    /// it. The latest value of each parameter is what is kept.
    bool setParam(std::uint32_t index, float value);
    /// Changes the ring had no room for.
    [[nodiscard]] std::uint64_t droppedParams() const;

    /// The host's heartbeat: how many cycles it has run.
    [[nodiscard]] std::uint64_t cycles() const;

    /// Asks for the plugin's state; it arrives as stateReceived().
    void requestState();
    /// Puts a state back.
    void setState(const QString &state);

    /// **Asks for the plugin's state and waits for it**, up to \a timeout:
    /// for saving, which has to write what the plugin holds now. A host
    /// answers within a millisecond or two; one that does not answer in
    /// time is a host whose last state is the best there is. Whatever else
    /// the host says meanwhile is still said, as signals.
    [[nodiscard]] std::optional<QString> stateNow(std::chrono::milliseconds timeout = std::chrono::milliseconds{250});

    /// **The plugin's own window**, shown and closed by the host. Answered
    /// by uiOpened(), or uiClosed() when it cannot be shown.
    void openUi();
    void closeUi();

    /// **The plugin's own presets**: listed (presetsListed()), one put in
    /// (its values arrive as valuesChanged(), then presetShown()), and the
    /// settings kept as one where other hosts of the format find them
    /// (presetSaved()).
    void listPresets();
    void loadPreset(const QString &presetId);
    void savePreset(const QString &name);

    /// **Watches for a host that has stopped running.** \a upstreamCycles
    /// says how many cycles the audio feeding the host has run; when it
    /// moves on by \a stalledCycles while the host's heartbeat stands still,
    /// for \a stalledFor at least, hung() is said, once.
    ///
    /// Both, because each alone is wrong somewhere: a count of cycles alone
    /// takes a host that a busy machine ran late three times at quantum 64
    /// — 4 ms — for a dead one, and a clock alone takes a graph that is not
    /// running at all, a suspended driver, for a hung host.
    ///
    /// **And a heartbeat still for \a stalledAtMost is hung whatever the
    /// audio before it did**. A host stopped while PipeWire is
    /// changing the graph — a new quantum, a link being made — can hold the
    /// whole graph still, the segment feeding it included, and then the
    /// count above never moves. The stillness has to be seen without a gap:
    /// a watch that was itself held up for longer than a tenth of a second —
    /// a machine asleep, a debugger — starts counting again, so waking up is
    /// never taken for a hang.
    void watch(std::function<std::uint64_t()> upstreamCycles,
               std::uint32_t stalledCycles = 3,
               std::chrono::milliseconds stalledFor = std::chrono::milliseconds{40},
               std::chrono::milliseconds stalledAtMost = std::chrono::milliseconds{1000});
    void unwatch();

Q_SIGNALS:
    /// The host's node is in the graph.
    void ready(const QString &nodeName, quint32 latencySamples);
    void latencyChanged(quint32 latencySamples);
    void stateReceived(const QString &state);
    /// A state sent with setState() is in, or \a loaded false when the host
    /// could not put it in and the plugin before it runs on. Whatever was
    /// held back meanwhile has gone to the host by the time this is said.
    void stateLoaded(bool loaded);
    /// The host could not do what it was started for; it exits after it.
    void failed(const QString &code);
    /// The process has gone. \a crashed unless it was asked to finish and
    /// did so cleanly.
    void exited(int exitCode, bool crashed);
    /// Its heartbeat stopped while the audio feeding it went on.
    void hung();

    /// The plugin's window is showing.
    void uiOpened();
    /// It has gone, or could not be shown: \a reason is "closed",
    /// "unavailable" or "failed" (also when no answer came in time).
    void uiClosed(const QString &reason);
    /// **A person moved a control in the plugin's window**: \a paramId is
    /// \a value as the plugin has it. The plugin has it already.
    void paramChanged(const QString &paramId, float value);
    /// A person took hold of that control, or let go of it.
    void paramGesture(const QString &paramId, bool begin);
    /// **The plugin's values, changed together**: \a edit when a person
    /// did it, otherwise what something asked of the host led to.
    void valuesChanged(bool edit, const QList<QPair<QString, float>> &values);
    /// The plugin's own presets, `{id, name}` each.
    void presetsListed(const QList<QPair<QString, QString>> &presets);
    /// The host could not send them (too many), or did not answer.
    void presetsRefused();
    /// The plugin's preset its settings are now, or empty for none.
    void presetShown(const QString &presetId);
    /// The id of the preset savePreset() wrote, or empty when it could not.
    void presetSaved(const QString &presetId);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::pw
