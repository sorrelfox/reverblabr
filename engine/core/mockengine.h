// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"
#include "core/presetstore.h"
#include "core/telemetry.h"

#include <QVariantList>

#include <memory>
#include <string>

namespace rb::dsp
{
class Block;
}

namespace rb::core
{

/// The engine, pretended.
///
/// **It is the same interface, answered from memory.** Sessions live in a
/// map rather than on disk, the inventory is made up, and Start does nothing
/// but say it did. That lets interface work begin before the engine it talks
/// to is finished, and it gives the tests of that interface somewhere to run
/// where there is no PipeWire at all — which is most places, including a
/// machine with no sound card.
///
/// **It is deliberately convincing about the awkward parts**: it numbers
/// patches, refuses an edit that does not make sense, complains about a
/// session that is not open, and reports failures as codes rather than text.
/// A client written against this and then pointed at the real engine should
/// notice nothing except that sound comes out.
class MockEngine : public EngineFacade
{
    Q_OBJECT

public:
    /// \a presets says where the person's own presets live; the default is
    /// their own directory. **A test gives its own**, so that running the
    /// tests neither reads what somebody already saved nor leaves anything
    /// behind — and so that two tests cannot see each other's work.
    explicit MockEngine(PresetStore presets = PresetStore{}, QObject *parent = nullptr);
    ~MockEngine() override;

    /// Puts some plausible applications and devices in the inventory, so an
    /// interface has something to draw before anything is running.
    ///
    /// **Invokable, because a QML test drives the mock directly.** The
    /// interface tests talk to this object through the facade exactly as
    /// the window does, and the made-up inventory is the thing they draw.
    Q_INVOKABLE void addFakeInventory();

    /// Adds a virtual output and a virtual microphone to the made-up
    /// machine, **listed the way the real inventory lists them**: the
    /// output as an output and, through its monitor, as an input; the
    /// microphone as an input and, through the ports that feed it, as an
    /// output. addFakeInventory() takes them away again.
    Q_INVOKABLE void addFakeVirtualDevices();

    /// Pairs a wireless headset with the made-up machine, or switches the
    /// one already there to another profile: its microphone as an input and
    /// its earpieces as an output, both wireless. **In its playback profile
    /// the microphone is listed but hears nothing**, which is what a real
    /// headset looks like when WirePlumber has not switched it, and the one
    /// thing about it an interface has to be built against without one.
    /// addFakeInventory() takes it away again.
    Q_INVOKABLE void addFakeHeadset(bool playbackProfile);

    /// Adds a virtual device to the made-up machine without asking for it,
    /// the way one somebody made earlier — or an administrator made for
    /// every user — is simply there. \a deviceJson is
    /// `{name, direction, channels, enabled, scope, status}`, each but the
    /// name optional: an enabled, present, stereo output of the user's own
    /// unless it says otherwise. Answers with the device's id.
    Q_INVOKABLE QString addFakeVirtualDevice(const QString &deviceJson);

    /// Says where a virtual device stands in the pretend graph: "present",
    /// "creating", "missing" or "waitingForPipeWire". **What a window has
    /// to show and cannot provoke on a working desktop.** A device that is
    /// not present leaves the made-up inventory.
    Q_INVOKABLE void setFakeVirtualDeviceStatus(const QString &deviceId, const QString &status);

    /// Refuses the next create, rename, switch or delete of a virtual
    /// device with \a reason, a pw::VirtualDeviceError's name — "NotOurs",
    /// "FileNotWritable" and the rest — as the real engine would when its
    /// file cannot be written. One refusal, then back to normal.
    Q_INVOKABLE void refuseNextVirtualDeviceChange(const QString &reason);

    /// Makes one scope's file ("user" or "system") unreadable for \a
    /// reason, or readable again with an empty one. While it is, that
    /// scope's devices are not listed and every change to one of the user's
    /// own is refused with the same reason — exactly the real engine's rule.
    Q_INVOKABLE void setFakeVirtualDeviceFileUnreadable(const QString &scope, const QString &reason);

    /// Replaces the made-up machine's all-users devices with \a devices,
    /// the list the helper would have written, in the form
    /// pw::virtualDevicesToVariant() gives. **What the helper does**, for a
    /// test that pretends the administrator's password was given: nothing
    /// is published until the engine is asked to reload, exactly as with
    /// the real file. A device already there keeps its status; a new one
    /// is present, or disabled. A list that is not one is ignored.
    Q_INVOKABLE void writeFakeSystemVirtualDevices(const QVariantList &devices);

    /// Pretends PipeWire would not make a device's node: it goes missing,
    /// and device.virtualNodeRefused is reported.
    Q_INVOKABLE void refuseFakeVirtualDeviceNode(const QString &deviceId);

    /// Forgets every virtual device, both scopes, with the unreadable files
    /// and a refusal still waiting, and lists the empty result. **For a
    /// test's cleanup**: the mock outlives each test function, and an
    /// all-users device is read-only, so nothing a window could ask for
    /// would take one away again.
    Q_INVOKABLE void clearFakeVirtualDevices();

    /// Takes one of the made-up devices out of the inventory, or puts it
    /// back. **What unplugging a headset looks like**, on a machine that
    /// has none: the device leaves the lists, a default that was pointing
    /// at it stops resolving, and the inventory is sent again at once.
    Q_INVOKABLE void setFakeDevicePresent(const QString &node, bool present);

    /// Pretends the audio system is there, or has gone away.
    ///
    /// **The one thing an interface cannot rehearse on a working desktop.**
    /// What a window does when PipeWire is not running — the banner, the
    /// Retry, the readings that go back to a dash — has to be built and
    /// tested somehow, and stopping the real server under a test suite is
    /// not somehow. With it away, starting a session fails the way it fails
    /// for real; reconnectAudioSystem() succeeds once it is back.
    ///
    /// Invokable so that a QML test can drive it, which is how the window's
    /// banner is tested at all.
    Q_INVOKABLE void setAudioSystemRunning(bool running);
    [[nodiscard]] Q_INVOKABLE bool isAudioSystemRunning() const;

    /// Reports an error as the engine would, code and arguments and no
    /// text. **For an interface being built against errors it cannot
    /// otherwise provoke**, a device that is unplugged among them. \a
    /// argsJson is the argument object the code declares; a "block"
    /// argument in it says which block the error is about.
    Q_INVOKABLE void reportError(const QString &code, const QString &argsJson);

    /// Where \a sessionId's made-up levels are published, or nullptr when
    /// nothing of that name is open. **Telemetry does not travel through the
    /// facade** ([ADR-0003]), so this is handed to AppShell separately,
    /// exactly as the real engine's telemetryOf() is.
    [[nodiscard]] const TelemetrySlot *telemetryOf(const QString &sessionId) const;

    /// The same slot for a window in another process, as the real engine's
    /// shareTelemetry() (ADR-0018): a read-only descriptor the caller owns,
    /// or -1.
    [[nodiscard]] int shareTelemetry(const QString &sessionId) const;

    /// Gives \a blockId a signal of \a peak, as an amplitude from 0 to 1,
    /// for as long as the session runs or until it is given another.
    ///
    /// **The one thing the mock could not pretend**. A wire is drawn
    /// in the live colour, and a tile's state dot lights, from what the
    /// telemetry says about the block the signal leaves; only a running
    /// session publishes that, so neither could be tested anywhere without
    /// PipeWire — and "the colour follows the signal within 100 ms" was met
    /// by construction rather than by anything that checked.
    ///
    /// The RMS that goes with it is the peak's, times one over root two,
    /// which is what a sine reads. A block nothing has been said about
    /// meters silence, like a block in a lane nothing is playing through.
    Q_INVOKABLE void setFakeLevel(const QString &sessionId, const QString &blockId, double peak);

    /// Gives \a blockId a live spectrum of \a bandsDb, one level in dB a
    /// band, published while the session runs and somebody has
    /// asked for that block's spectrum with analyse() — as the real engine
    /// publishes one only then.
    Q_INVOKABLE void setFakeSpectrum(const QString &sessionId, const QString &blockId, const QVariantList &bandsDb);
    /// The same for the spectrum **before** the block. A block
    /// nothing has been said about has none, as a block with no inputs.
    Q_INVOKABLE void
    setFakeInputSpectrum(const QString &sessionId, const QString &blockId, const QVariantList &bandsDb);
    /// Gives \a blockId band values, up to rt::kMaxBandValues,
    /// published beside its spectra while it is analysed.
    Q_INVOKABLE void setFakeBandValues(const QString &sessionId, const QString &blockId, const QVariantList &values);
    /// **Laid over the diagnostics the mock makes up**: each
    /// top-level key of \a diagnosticsJson replaces the mock's own, so a
    /// test can give sessions a load, late cycles and a real-time answer,
    /// and the graph some nodes and links. An empty string takes it off.
    Q_INVOKABLE void setFakeDiagnostics(const QString &diagnosticsJson);
    /// Which block's spectrum was last asked for, for a test to see that a
    /// panel asked, and that it let go when it closed.
    [[nodiscard]] Q_INVOKABLE QString analysed(const QString &sessionId) const;
    /// And of which channel: kBothChannels, 0 or 1.
    [[nodiscard]] Q_INVOKABLE int analysedChannel(const QString &sessionId) const;

    /// Takes the signal off every block of \a sessionId. What the lane
    /// going quiet looks like, which is the other half of a test that the
    /// colour follows the signal.
    Q_INVOKABLE void clearFakeLevels(const QString &sessionId);

    /// **A block type of a test's own**, beside the built-in ones:
    /// for testing what every block of a kind shows — a hearing block's ear
    /// badge — before any block of that kind ships. Not invokable, so a QML
    /// file cannot add one: the test runner does, before the QML starts, and
    /// the catalogue is sent again. \a factory is a dsp::BlockRegistry one.
    void addBlockTypeForTesting(const std::string &typeId, std::unique_ptr<dsp::Block> (*factory)());

    /// **A plugin as if a scan had found it**. The mock starts with
    /// the host's built-in plugins, which a real scan finds too.
    Q_INVOKABLE void addFakePlugin(const QString &format,
                                   const QString &id,
                                   const QString &name,
                                   const QString &vendor,
                                   int audioInputs = 2,
                                   int audioOutputs = 2,
                                   const QString &paramsJson = {});
    /// A candidate as if a scan had blocklisted it, "crashed" or "hung".
    Q_INVOKABLE void addFakeBlockedPlugin(const QString &format, const QString &path, const QString &reason);
    /// A candidate as if a scan had got only part of the way through it.
    Q_INVOKABLE void addFakeIncompletePlugins(const QString &format, const QString &path);
    /// Back to the built-in plugins alone, and nothing blocklisted.
    Q_INVOKABLE void clearFakePlugins();
    /// How many rescans were asked for, and whether the last one retried
    /// the blocklisted.
    [[nodiscard]] Q_INVOKABLE int fakeRescans() const;
    [[nodiscard]] Q_INVOKABLE bool fakeLastRescanRetried() const;

    /// Where addFakeRecording() puts its files: a folder of the mock's own,
    /// removed with it.
    [[nodiscard]] Q_INVOKABLE QString fakeRecordingFolder() const;

    /// **A recording \a sessionId has made**, as a real WAV of \a seconds
    /// called \a name in fakeRecordingFolder(), entered in the mock's own
    /// recordings index as the engine enters one: started, and finished
    /// unless \a inProgress. Gives back its path, or nothing if the file
    /// could not be written. Its waveform is read in the background;
    /// waitForFakeRecordings() waits for it.
    Q_INVOKABLE QString addFakeRecording(const QString &sessionId,
                                         const QString &name,
                                         double seconds,
                                         bool inProgress = false);
    /// **An impulse response for a FIR Filter to be given**: a real
    /// WAV called \a name in fakeRecordingFolder(), \a channels of a click
    /// dying away over \a seconds at \a rate. Gives back its path, or
    /// nothing if it could not be written.
    Q_INVOKABLE QString writeFakeImpulse(const QString &name, int channels, double seconds, int rate = 48000);

    /// Finishes one addFakeRecording() left in progress.
    Q_INVOKABLE void finishFakeRecording(const QString &sessionId, const QString &path);
    /// Waits until every fake recording's file has been read back.
    Q_INVOKABLE void waitForFakeRecordings();

    /// What listSessions() says, as its JSON array, without saying it, as
    /// Engine::sessionListing() does.
    [[nodiscard]] QString sessionListing() const;

    /// The sessions whose captured applications a timer has asked to quit,
    /// in order: the mock has no processes to quit.
    [[nodiscard]] QStringList applicationsQuitFor() const;

public Q_SLOTS:
    void listSessions() override;
    void createSession(const QString &name) override;
    void openSession(const QString &sessionId) override;
    void saveSession(const QString &sessionId) override;
    void closeSession(const QString &sessionId) override;
    void deleteSession(const QString &sessionId) override;
    void exportSession(const QString &sessionId, const QString &path) override;
    void importSession(const QString &path) override;

    void listPresets(const QString &typeId) override;
    void applyPreset(const QString &sessionId, const QString &blockId, const QString &presetId) override;
    void savePreset(const QString &sessionId, const QString &blockId, const QString &name, bool replace) override;
    void renamePreset(const QString &typeId, const QString &presetId, const QString &name) override;
    void deletePreset(const QString &typeId, const QString &presetId) override;

    void applyEdit(const QString &sessionId, const QString &editJson) override;
    void setParam(const QString &sessionId,
                  const QString &blockId,
                  const QString &paramId,
                  const QString &valueJson) override;
    void undo(const QString &sessionId) override;
    void redo(const QString &sessionId) override;
    void getUndoState(const QString &sessionId) override;

    void controlRecording(const QString &sessionId, const QString &blockId, const QString &action) override;
    void restartPlugin(const QString &sessionId, const QString &blockId) override;
    void analyse(const QString &sessionId, const QString &blockId, int channel) override;
    void start(const QString &sessionId) override;
    void stop(const QString &sessionId) override;
    void getSessionState(const QString &sessionId) override;
    void resynchronise(const QString &sessionId) override;

    void getInventory() override;
    void getBlockCatalogue() override;

    void getAudioSystem() override;
    void reconnectAudioSystem() override;
    void getDiagnostics() override;

    void listVirtualDevices() override;
    void createVirtualDevice(const QString &deviceJson) override;
    void renameVirtualDevice(const QString &deviceId, const QString &name) override;
    void setVirtualDeviceEnabled(const QString &deviceId, bool enabled) override;
    void deleteVirtualDevice(const QString &deviceId) override;
    void reloadVirtualDevices() override;
    void listRecordings(const QString &sessionId) override;
    void rescanRecordings() override;
    void getRecording(const QString &path) override;
    void renameRecording(const QString &path, const QString &name) override;
    void setRecordingTags(const QString &path, const QString &tagsJson) override;
    void previewRecording(const QString &path) override;
    void pausePreview() override;
    void seekPreview(double seconds) override;
    void stopPreview() override;
    void listPlugins() override;
    void rescanPlugins(bool retryBlocked) override;
    void describePlugin(const QString &format, const QString &id) override;
    void listTimers() override;
    void fireTimer(const QString &timerId) override;

public:
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::core
