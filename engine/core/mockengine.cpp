// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/mockengine.h"

#include "blocks/autolevel.h"
#include "blocks/broadcast.h"
#include "blocks/builtinblocks.h"
#include "blocks/declick.h"
#include "blocks/dehum.h"
#include "blocks/denoise.h"
#include "blocks/firfilter.h"
#include "blocks/inputdevice.h"
#include "blocks/limiter.h"
#include "blocks/multibandcompressor.h"
#include "blocks/outputdevice.h"
#include "buildversion.h"
#ifdef RB_HAVE_RNNOISE
#include "blocks/speechdenoise.h"
#endif
#include "blocks/sync.h"
#include "blocks/timeshift.h"
#include "blocks/vumeter.h"
#include "core/audiosystem.h"
#include "core/blockcatalogue.h"
#include "core/broadcasts.h"
#ifdef RB_HAVE_LIVESTREAM
#include "core/livestreams.h"
#endif
#include "core/diagnostics.h"
#include "core/editcommand.h"
#include "core/edithistory.h"
#include "core/errorcodes.h"
#include "core/logring.h"
#include "core/pluginscanner.h"
#include "core/preset.h"
#include "core/presetactions.h"
#include "core/presetstore.h"
#include "core/recordinglibrary.h"
#include "core/recordingnames.h"
#include "core/recordings.h"
#include "core/session.h"
#include "core/sessionlisting.h"
#include "core/sessionplan.h"
#include "core/sessionstore.h"
#include "core/sharedtelemetry.h"
#include "core/telemetry.h"
#include "core/timerrunner.h"
#include "core/timers.h"
#include "core/virtualdevices.h"
#include "pw/inventory.h"
#include "pw/virtualdevicemanager.h"
#include "record/audiofile.h"
#include "record/audiofilereader.h"
#include "record/impulsefile.h"
#include "rt/meter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

[[nodiscard]] QString asText(std::string_view name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString asJson(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

/// A device for the made-up inventory, with the channels it is said to
/// have. **Not every device is a stereo pair**, and a picker that has only
/// ever been shown stereo pairs is a picker whose channel lists have never
/// been looked at.
[[nodiscard]] pw::InventoryDevice
device(const QString &name, const QString &description, bool isDefault, const QStringList &channels)
{
    pw::InventoryDevice made;
    made.name = name;
    made.description = description;
    made.channels = channels;
    made.isDefault = isDefault;
    return made;
}

/// An application for the made-up inventory, playing one stream.
[[nodiscard]] pw::InventoryApp application(const QString &binary, const QString &displayName, const QString &sink)
{
    pw::InventoryApp made;
    made.key = "binary:"_L1 + binary;
    made.displayName = displayName;
    made.iconName = binary;
    made.binary = binary;
    made.sinks = QStringList{sink};

    pw::InventoryStream stream;
    stream.name = binary + ".stream"_L1;
    stream.mediaName = displayName;
    stream.sinks = made.sinks;
    made.streams.append(stream);
    return made;
}

/// What the mock says about the machine while a session runs. Steady on
/// purpose: nothing here is measured, and a load that wandered would make
/// every test of the status bar chase it.
constexpr std::uint32_t kFakeSampleRate = 48000;
constexpr std::uint32_t kFakeQuantum = 1024;
constexpr float kFakeDspLoad = 0.05f;
constexpr std::uint32_t kFakeChannels = 2;
/// A sine's RMS as a share of its peak, one over root two.
constexpr float kSineRmsOfPeak = 0.7071068f;

} // namespace

class MockEngine::Private
{
public:
    /// What a FIR Filter publishes about its file, and which file that was.
    struct FirReading {
        QString path;
        float shape = blocks::FirFilter::kNoFile;
        float taps = 0.0f;
    };

    struct Open {
        Session session;
        QString state = QString(sessionState::idle);
        /// When it last began running, as the real engine keeps it.
        qint64 startedAtMs = 0;
        /// The real history, not a pretend one: undo is one of the things
        /// an interface has to be written against before the engine is
        /// finished.
        EditHistory history;
        UndoState announced;
        /// Where this session's made-up levels are published: in shared
        /// memory, as the real engine's are (ADR-0018), so a window on a
        /// mock daemon reads them too. **It never moves**, because the
        /// interface keeps the address it was handed for as long as it is
        /// watching.
        PublishedSlot telemetry;
        /// The peak amplitude a test has given each block, if any.
        QHash<QString, float> levels;
        /// The spectrum a test has given each block, and whose is asked
        /// for.
        QHash<QString, QVariantList> spectra;
        QHash<QString, QVariantList> inputSpectra;
        QHash<QString, QVariantList> bandValues;
        QString analysed;
        int analysedChannel = kBothChannels;
        /// What each FIR Filter's file holds, read again only when the
        /// path changes.
        QHash<QString, FirReading> responses;
    };

    MockEngine *q = nullptr;
    std::map<QString, Open> open;
    /// **Broadcast blocks' passwords, in memory**: the mock never touches
    /// the desktop's secret store. By session and block.
    QHash<QString, QString> broadcastPasswords;
    /// Everything the mock pretends the machine has.
    pw::Inventory made;
    /// The node names that are unplugged at the moment. Kept apart from
    /// `made` so that a device can come back with everything it had.
    QStringList absent;
    /// The inventory as it was last sent, which is what getInventory()
    /// answers with.
    QJsonObject inventory;
    /// Whether the pretend audio system is there. It is, until a test or a
    /// demonstration says otherwise.
    bool audioSystemRunning = true;
    /// Whether we are attached to it. **Not the same question**: a server
    /// that has come back is not a connection, and something has to ask for
    /// one — which is what the Retry in the interface is for.
    bool attached = true;
    /// **The real blocks.** The mock's whole job is to be convincing, and a
    /// made-up list of parameters is the one thing an interface cannot be
    /// built against: the settings panel is generated from exactly these.
    dsp::BlockRegistry registry;
    /// Where presets live: the ones that ship and the person's own.
    PresetStore presets;
    /// The virtual devices, both scopes, the user's first. **Answered from
    /// memory by the same rules the real manager keeps**: names judged by
    /// the same function, all-users devices read-only, an unreadable file
    /// refusing every change to its scope.
    QList<pw::VirtualDeviceState> virtualDevices;
    /// The scopes whose file the pretend machine cannot read, and why.
    std::map<pw::VirtualDeviceScope, pw::VirtualDeviceError> unreadable;
    /// The refusal the next change to a device meets, once.
    std::optional<pw::VirtualDeviceError> refuseNext;

    /// Publishes a snapshot for every running session, sixty times a
    /// second — the rate the real sampler runs at, so that the interface's
    /// hold and decay are aged over the same number of frames here as they
    /// are against the engine.
    QTimer telemetryTimer;
    /// What a timer does when its time comes, as the engine has it.
    std::unique_ptr<TimerRunner> timers;
    QStringList applicationsQuitFor;
    /// The monotonic clock a snapshot is stamped with. The interface's peak
    /// hold is in wall-clock time, so the stamps have to move.
    QElapsedTimer clock;
    std::uint64_t frame = 0;

    /// **A recordings index of the mock's own**, in a folder that goes
    /// with it, with the files a test makes beside it: an interface built
    /// against the mock lists real files and trashes real files, and never
    /// the person's own.
    QTemporaryDir recordingsHome;
    RecordingLibrary library{recordingsHome.filePath(u"library.db"_s)};
    /// What a plugin scan would have found.
    QList<InstalledPlugin> fakePlugins;
    /// What a test laid over the made-up diagnostics.
    QJsonObject fakeDiagnostics;
    QList<BlockedCandidate> fakeBlocked;
    QList<BlockedCandidate> fakeIncomplete;
    int rescans = 0;
    bool lastRescanRetried = false;

    void resetFakePlugins()
    {
        const QJsonArray gain = QJsonArray{QJsonObject{{"id"_L1, u"gain"_s},
                                                       {"name"_L1, u"gain"_s},
                                                       {"kind"_L1, u"float"_s},
                                                       {"min"_L1, 0.0},
                                                       {"max"_L1, 4.0},
                                                       {"default"_L1, 1.0}}};
        fakePlugins = {
            InstalledPlugin{u"builtin"_s,
                            u"test.gain"_s,
                            u"Test gain"_s,
                            u"Reverblabr"_s,
                            u"Utility"_s,
                            2,
                            2,
                            1,
                            u"builtin"_s,
                            gain},
            InstalledPlugin{u"builtin"_s,
                            u"test.slowgain"_s,
                            u"Test gain, slow to load"_s,
                            u"Reverblabr"_s,
                            u"Utility"_s,
                            2,
                            2,
                            1,
                            u"builtin"_s,
                            gain},
        };
        fakeBlocked.clear();
        fakeIncomplete.clear();
    }

    /// **A pretend preview**: no sound, but the same states and the
    /// same answers the engine's player gives, with the position moving in
    /// real time, so a play button can be built and tested against it.
    struct Preview {
        QString path;
        QString state = u"stopped"_s;
        double duration = 0.0;
        double heldSeconds = 0.0;
        QElapsedTimer since;
        QTimer tick;
    } preview;

    [[nodiscard]] double previewSeconds() const
    {
        if (preview.state != "playing"_L1) {
            return preview.heldSeconds;
        }
        return std::min(preview.duration, preview.heldSeconds + preview.since.elapsed() / 1000.0);
    }

    void announcePreview()
    {
        const QJsonObject status{
            {u"path"_s, preview.path},
            {u"state"_s, preview.state},
            {u"seconds"_s, previewSeconds()},
            {u"duration"_s, preview.duration},
        };
        Q_EMIT q->previewChanged(asJson(status));
    }

    void setPreviewState(const QString &state)
    {
        preview.heldSeconds = previewSeconds();
        preview.state = state;
        preview.since.restart();
        if (state == "playing"_L1) {
            preview.tick.start();
        } else {
            preview.tick.stop();
        }
        announcePreview();
    }

    explicit Private(MockEngine *owner, PresetStore presets)
        : q(owner)
        , presets(std::move(presets))
    {
        (void)blocks::registerBuiltinBlocks(registry);
        clock.start();
        telemetryTimer.setInterval(TelemetrySampler::kPeriod);
        telemetryTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&telemetryTimer, &QTimer::timeout, q, [this] { publishTelemetry(); });
    }

    /// Starts or stops the publishing depending on whether anything is
    /// running. **A stopped session publishes nothing at all**, which is
    /// what the interface reads as a dash rather than as a zero, and is why
    /// a meter does not sit at silence when nobody has pressed Start.
    void followRunningSessions()
    {
        const bool anyRunning = std::any_of(
            open.begin(), open.end(), [](const auto &entry) { return entry.second.state == sessionState::running; });
        if (anyRunning && !telemetryTimer.isActive()) {
            telemetryTimer.start();
        } else if (!anyRunning && telemetryTimer.isActive()) {
            telemetryTimer.stop();
        }
    }

    /// Lays out one snapshot per running session and stores it.
    void publishTelemetry()
    {
        for (auto &entry : open) {
            if (entry.second.state == sessionState::running) {
                publish(entry.second, true);
            }
        }
    }

    /// Writes one snapshot for \a session. With \a running false every
    /// block reads silence, which is **the snapshot that takes the meters
    /// on the screen down** when a session stops — the same last word the
    /// real sampler has on its way out.
    void publish(Open &session, bool running)
    {
        TelemetrySnapshot snapshot;
        snapshot.frame = ++frame;
        snapshot.nanos = static_cast<std::uint64_t>(clock.nsecsElapsed());
        snapshot.running = running;
        if (running) {
            // Plausible and steady, because nothing here is measuring
            // anything: a mock whose load wandered would have every test of
            // the status bar chasing a number.
            snapshot.sampleRate = kFakeSampleRate;
            snapshot.frames = kFakeQuantum;
            snapshot.dspLoad = kFakeDspLoad;
            snapshot.peakDspLoad = kFakeDspLoad;
        }
        // **Only the block asked for, and only while running**, as the real
        // sampler publishes one.
        if (running && !session.analysed.isEmpty()) {
            const QByteArray id = session.analysed.toUtf8().left(kMaxTelemetryIdBytes - 1);
            std::copy(id.cbegin(), id.cend(), static_cast<char *>(snapshot.analysedId));
            snapshot.analysedChannel = session.analysedChannel;
            const auto fill = [](const QVariantList &from, float (&into)[kSpectrumBands]) {
                for (std::size_t band = 0; band < kSpectrumBands; ++band) {
                    into[band] = band < static_cast<std::size_t>(from.size())
                        ? from.at(static_cast<qsizetype>(band)).toFloat()
                        : -120.0f;
                }
            };
            snapshot.spectrumBands = static_cast<std::uint32_t>(kSpectrumBands);
            fill(session.spectra.value(session.analysed), snapshot.spectrum);
            // **Before the block only when there is one to give**, as the
            // real sampler leaves it out for a block with no inputs.
            if (session.inputSpectra.contains(session.analysed)) {
                snapshot.inputSpectrumBands = static_cast<std::uint32_t>(kSpectrumBands);
                fill(session.inputSpectra.value(session.analysed), snapshot.inputSpectrum);
            }
            const QVariantList values = session.bandValues.value(session.analysed);
            snapshot.bandValueCount =
                static_cast<std::uint32_t>(std::min<qsizetype>(values.size(), rt::kMaxBandValues));
            for (std::uint32_t band = 0; band < snapshot.bandValueCount; ++band) {
                snapshot.bandValues[band] = values.at(band).toFloat();
            }
        }
        for (const BlockInstance &block : session.session.blocks) {
            if (snapshot.blockCount >= kMaxMeteredBlocks) {
                break;
            }
            BlockTelemetry &into = snapshot.blocks[snapshot.blockCount];
            const QByteArray id = block.id.toUtf8().left(kMaxTelemetryIdBytes - 1);
            std::copy(id.cbegin(), id.cend(), static_cast<char *>(into.id));
            into.enabled = block.enabled;
            into.channelCount = kFakeChannels;
            const float peak = running ? session.levels.value(block.id, 0.0f) : 0.0f;
            // A sine's RMS, which is what a made-up level should read as:
            // anything else would make a Peak/RMS face show a pair of
            // numbers no signal ever produces.
            for (std::uint32_t channel = 0; channel < kFakeChannels; ++channel) {
                into.peak[channel] = peak;
                into.rms[channel] = peak * kSineRmsOfPeak;
            }
            into.clipped = peak >= 1.0f;
            into.active = peak >= rt::MeterReading::kActivity;
            // A device block says whether its device is there, as the real
            // engine's does: a tile reads "Unavailable" from a zero.
            if (running && isDeviceBlock(block)) {
                into.values[blocks::OutputDevice::kDevicePresentValue] = devicePresent(block) ? 1.0f : 0.0f;
            }
            // A VU needle settled on a steady sine reads the sine's peak,
            // which is what the real block publishes for one.
            if (block.type == asText(blocks::VuMeter::kTypeId)) {
                into.values[blocks::VuMeter::kLevelLeftValue] = peak;
                into.values[blocks::VuMeter::kLevelRightValue] = peak;
            }
            // A Sync block says what it is holding the lane back by, in
            // samples at the made-up rate, as the real one does.
            if (running && block.type == asText(blocks::Sync::kTypeId)) {
                into.values[blocks::Sync::kDelaySamplesValue] = static_cast<float>(
                    std::round(block.params.value("delay"_L1).toDouble() * 1.0e-3 * kFakeSampleRate));
            }
            // An Auto Level boosts a made-up level the way the real block
            // boosts a steady one: by how far it is under the target, within
            // the maximum, and not at all under the gate or switched off.
            // A Limiter takes a made-up level down to its ceiling, as the
            // real one takes a steady tone: by how far its peak is over.
            // A Denoise says whether it has a noise to take out. The mock
            // has no audio to learn from, so a Learn press is taken as
            // learned at once, three seconds' worth, and a Forget as gone.
            // A Declick has nothing to find in a made-up level: none taken
            // out, as the real one says of a clean signal.
            if (running && block.type == asText(blocks::Declick::kTypeId)) {
                into.values[blocks::Declick::kClicksValue] = 0.0f;
            }
            // A Dehum has no hum to lock onto in a made-up level either: its
            // notches stay on Base, and with Auto on it says it is holding.
            if (running && block.type == asText(blocks::Dehum::kTypeId)) {
                into.values[blocks::Dehum::kBaseValue] =
                    static_cast<float>(block.params.value("base"_L1).toDouble(blocks::Dehum::kDefaultBase));
                into.values[blocks::Dehum::kLockedValue] = block.params.value("auto"_L1).toBool() ? 0.0f : -1.0f;
            }
            if (running && block.type == asText(blocks::Denoise::kTypeId)) {
                const bool learned = !block.params.value("profile"_L1).toString().isEmpty()
                    || block.params.value("learn"_L1).toInt() > block.params.value("forget"_L1).toInt();
                into.values[blocks::Denoise::kStateValue] = learned ? blocks::Denoise::kActive : blocks::Denoise::kIdle;
                into.values[blocks::Denoise::kLearnedSecondsValue] = learned ? blocks::Denoise::kLearnSeconds : 0.0f;
            }
            // A FIR Filter says what its file is, read through the same
            // reader the real block uses: the shape and the length at the
            // made-up rate, as it is before trimming.
            if (running && block.type == asText(blocks::FirFilter::kTypeId)) {
                const FirReading &reading = firReading(session, block);
                into.values[blocks::FirFilter::kShapeValue] = reading.shape;
                into.values[blocks::FirFilter::kTapsValue] = reading.taps;
                into.values[blocks::FirFilter::kSecondsValue] = reading.taps / static_cast<float>(kFakeSampleRate);
                into.values[blocks::FirFilter::kDelayMsValue] =
                    static_cast<float>(blocks::FirFilter::kPartition) * 1000.0f / static_cast<float>(kFakeSampleRate);
            }
            // A Time Shift has kept its whole history, as one that has run
            // that long would, and its head is where the last jump put it:
            // the mock has no audio for a pause to fall behind.
            if (running && block.type == asText(blocks::TimeShift::kTypeId)) {
                const double kept =
                    block.params.value("minutes"_L1).toDouble(static_cast<double>(blocks::TimeShift::kDefaultMinutes))
                    * 60.0;
                const double offset = block.params.value("jump"_L1).toInt() > 0
                    ? std::clamp(block.params.value("target"_L1).toDouble(), 0.0, kept)
                    : 0.0;
                into.values[blocks::TimeShift::kOffsetValue] = static_cast<float>(offset);
                into.values[blocks::TimeShift::kAvailableValue] = static_cast<float>(kept);
                into.values[blocks::TimeShift::kPausedValue] = block.params.value("paused"_L1).toBool() ? 1.0f : 0.0f;
            }
#ifdef RB_HAVE_RNNOISE
            // A Speech Denoise hears a voice in whatever a test has given
            // it a level, at the made-up rate, which is the model's own.
            if (running && block.type == asText(blocks::SpeechDenoise::kTypeId)) {
                into.values[blocks::SpeechDenoise::kStateValue] = blocks::SpeechDenoise::kActive;
                into.values[blocks::SpeechDenoise::kVoiceValue] = peak > 0.0f ? 1.0f : 0.0f;
                into.values[blocks::SpeechDenoise::kGateValue] = peak > 0.0f ? 1.0f : 0.0f;
            }
#endif
            if (running && block.type == asText(blocks::Limiter::kTypeId)) {
                into.values[blocks::Limiter::kGainReductionValue] = limiterReduction(block, peak);
            }
            if (running && block.type == asText(blocks::AutoLevel::kTypeId)) {
                into.values[blocks::AutoLevel::kBoostValue] = autoLevelBoost(block, peak);
                into.values[blocks::AutoLevel::kLoudnessValue] = fakeLoudness(peak);
            }
            // A Multiband Compressor holds a made-up level back in each band
            // as the real one holds a steady tone in that band: by the band's
            // own curve.
            if (running && block.type == asText(blocks::MultibandCompressor::kTypeId)) {
                for (std::uint32_t band = 0; band < blocks::MultibandCompressor::kBands; ++band) {
                    into.values[blocks::MultibandCompressor::kGainReductionValue + band] =
                        multibandReduction(block, peak, band);
                }
            }
            ++snapshot.blockCount;
        }
        session.telemetry.slot().store(snapshot);
    }

    static_assert(blocks::OutputDevice::kDevicePresentValue == blocks::InputDevice::kDevicePresentValue,
                  "the two device blocks say whether their device is there in the same place");

    [[nodiscard]] static const FirReading &firReading(Open &session, const BlockInstance &block)
    {
        const QString path = block.params.value("file"_L1).toString();
        FirReading &reading = session.responses[block.id];
        if (reading.path == path && (reading.shape != blocks::FirFilter::kNoFile || path.isEmpty())) {
            return reading;
        }
        reading = FirReading{path};
        if (path.isEmpty()) {
            return reading;
        }
        const auto read = record::readImpulseFile(path.toStdString());
        const std::size_t channels = read ? read->channels.size() : 0;
        if (channels != 1 && channels != 2 && channels != 4) {
            reading.shape = blocks::FirFilter::kUnusable;
            return reading;
        }
        reading.shape = static_cast<float>(channels);
        reading.taps = std::round(static_cast<float>(read->frames()) * static_cast<float>(kFakeSampleRate)
                                  / static_cast<float>(read->sampleRate));
        return reading;
    }

    /// A made-up level's loudness: a sine's RMS in dB, which is what a
    /// steady tone near 1 kHz reads to within a fraction of a decibel.
    [[nodiscard]] static float fakeLoudness(float peak)
    {
        const float rms = peak * kSineRmsOfPeak;
        return rms > 0.0f ? std::max(-120.0f, 20.0f * std::log10(rms)) : -120.0f;
    }

    [[nodiscard]] static float limiterReduction(const BlockInstance &block, float peak)
    {
        if (peak <= 0.0f) {
            return 0.0f;
        }
        const auto ceiling =
            static_cast<float>(block.params.value("ceiling"_L1).toDouble(blocks::Limiter::kDefaultCeilingDb));
        return std::max(0.0f, 20.0f * std::log10(peak) - ceiling);
    }

    [[nodiscard]] static float multibandReduction(const BlockInstance &block, float peak, std::uint32_t band)
    {
        if (peak <= 0.0f) {
            return 0.0f;
        }
        // What the session does not mention is the block's default.
        static const blocks::MultibandCompressor defaults;
        const auto number = [&block](const std::string &id) {
            const float fallback = defaults.param(id).asFloat().value_or(0.0f);
            return static_cast<float>(block.params.value(QString::fromStdString(id)).toDouble(fallback));
        };
        const std::string name{blocks::MultibandCompressor::kBandNames[band]};
        dsp::DynamicsSettings settings;
        settings.thresholdDb = number(name + "Threshold");
        settings.ratio = number(name + "Ratio");
        settings.kneeDb = number("knee");
        return dsp::gainReductionDb(20.0f * std::log10(peak), settings);
    }

    [[nodiscard]] static float autoLevelBoost(const BlockInstance &block, float peak)
    {
        const float loudness = fakeLoudness(peak);
        if (!block.params.value("boost"_L1).toBool(true) || loudness < blocks::AutoLevel::kGateLufs) {
            return 0.0f;
        }
        const auto target =
            static_cast<float>(block.params.value("target"_L1).toDouble(blocks::AutoLevel::kDefaultTargetLufs));
        const auto maximum = static_cast<float>(
            block.params.value("maximumBoost"_L1).toDouble(blocks::AutoLevel::kDefaultMaximumBoostDb));
        return std::clamp(target - loudness, 0.0f, maximum);
    }

    [[nodiscard]] static bool isOutput(const BlockInstance &block)
    {
        return block.type == asText(blocks::OutputDevice::kTypeId);
    }

    [[nodiscard]] static bool isDeviceBlock(const BlockInstance &block)
    {
        return isOutput(block) || block.type == asText(blocks::InputDevice::kTypeId);
    }

    /// Whether the device \a block is pointed at is on the made-up machine
    /// now, **worked out the way the real engine's plan works it out**
    /// (`sessionplan.cpp`): "default", or nothing, is whatever the default
    /// is at the moment, and a device is there when it is plugged in. A
    /// default that has gone with its device is nothing to reach.
    [[nodiscard]] bool devicePresent(const BlockInstance &block) const
    {
        const bool output = isOutput(block);
        const QString chosen = block.params.value("device"_L1).toString();
        const bool followsDefault = chosen.isEmpty()
            || chosen == asText(output ? blocks::OutputDevice::kDefaultDevice : blocks::InputDevice::kDefaultDevice);
        const QString wanted = followsDefault ? (output ? made.defaultSink : made.defaultSource) : chosen;
        if (wanted.isEmpty() || absent.contains(wanted)) {
            return false;
        }
        const pw::Inventory everything = withVirtualDevices();
        const QList<pw::InventoryDevice> &devices = output ? everything.outputs : everything.inputs;
        return std::ranges::any_of(devices,
                                   [&wanted](const pw::InventoryDevice &device) { return device.name == wanted; });
    }

    /// The made-up machine with the virtual devices that are present on it,
    /// **listed the way the real inventory lists them**: an output as
    /// an output and, through its monitor, as an input; a microphone as an
    /// input and, through the ports that feed it, as an output.
    [[nodiscard]] pw::Inventory withVirtualDevices() const
    {
        pw::Inventory everything = made;
        for (const pw::VirtualDeviceState &state : virtualDevices) {
            if (state.status != pw::VirtualDeviceStatus::Present || unreadable.contains(state.device.scope)) {
                continue;
            }
            const QStringList channels = state.device.channels == pw::VirtualDeviceChannels::Mono
                ? QStringList{u"MONO"_s}
                : QStringList{u"FL"_s, u"FR"_s};
            pw::InventoryDevice listed =
                device(pw::virtualDeviceNodeName(state.device.id), state.device.name, false, channels);
            listed.mediaClass = pw::virtualDeviceMediaClass(state.device.direction);
            listed.isVirtual = true;
            if (state.device.direction == pw::VirtualDeviceDirection::Output) {
                everything.outputs.append(listed);
                listed.isMonitor = true;
                everything.inputs.append(listed);
            } else {
                everything.inputs.append(listed);
                everything.outputs.append(listed);
            }
        }
        return everything;
    }

    /// The devices of the scopes that can be read, as the facade lists them.
    [[nodiscard]] QList<pw::VirtualDeviceState> listedVirtualDevices() const
    {
        QList<pw::VirtualDeviceState> listed;
        for (const pw::VirtualDeviceState &state : virtualDevices) {
            if (!unreadable.contains(state.device.scope)) {
                listed.append(state);
            }
        }
        return listed;
    }

    void publishVirtualDevices()
    {
        const auto scopeError = [this](pw::VirtualDeviceScope scope) -> std::optional<pw::VirtualDeviceError> {
            const auto found = unreadable.find(scope);
            return found == unreadable.end() ? std::nullopt : std::optional(found->second);
        };
        Q_EMIT q->virtualDevicesChanged(virtualDevicesMessage(listedVirtualDevices(),
                                                              scopeError(pw::VirtualDeviceScope::User),
                                                              scopeError(pw::VirtualDeviceScope::System)));
    }

    [[nodiscard]] pw::VirtualDeviceState *findVirtualDevice(const QString &deviceId)
    {
        for (pw::VirtualDeviceState &state : virtualDevices) {
            if (state.device.id == deviceId && !unreadable.contains(state.device.scope)) {
                return &state;
            }
        }
        return nullptr;
    }

    [[nodiscard]] QList<pw::VirtualDevice> allVirtualDevices() const
    {
        QList<pw::VirtualDevice> all;
        for (const pw::VirtualDeviceState &state : virtualDevices) {
            all.append(state.device);
        }
        return all;
    }

    /// The refusal a change to one of the user's own devices meets before
    /// anything else is looked at: the one a test asked for, or the user
    /// file being unreadable — the real manager will not write over a file
    /// it could not read.
    [[nodiscard]] std::optional<pw::VirtualDeviceError> refusalFirst()
    {
        if (refuseNext) {
            return std::exchange(refuseNext, std::nullopt);
        }
        if (const auto found = unreadable.find(pw::VirtualDeviceScope::User); found != unreadable.end()) {
            return found->second;
        }
        return std::nullopt;
    }

    /// Carries out one change to a device and answers it, like the engine:
    /// the list when it was made, device.virtualRefused when it was not.
    void
    changeVirtualDevice(const QString &deviceId,
                        const std::function<std::optional<pw::VirtualDeviceError>(pw::VirtualDeviceState &)> &change)
    {
        pw::VirtualDeviceState *state = findVirtualDevice(deviceId);
        const QString name = state == nullptr ? deviceId : state->device.name;
        std::optional<pw::VirtualDeviceError> refusal = refusalFirst();
        if (!refusal && state == nullptr) {
            refusal = pw::VirtualDeviceError::NotFound;
        }
        if (!refusal && state->device.scope == pw::VirtualDeviceScope::System) {
            refusal = pw::VirtualDeviceError::ReadOnlyScope;
        }
        if (!refusal) {
            refusal = change(*state);
        }
        if (refusal) {
            fail(ErrorCode::VirtualDeviceRefused, {name, virtualDeviceReason(*refusal)});
            return;
        }
        publishVirtualDevices();
        publishInventory();
    }

    /// Works out what the lists look like with the unplugged devices left
    /// out, and sends it.
    void publishInventory()
    {
        pw::Inventory shown = withVirtualDevices();
        const auto unplug = [this](QList<pw::InventoryDevice> &devices) {
            devices.removeIf([this](const pw::InventoryDevice &device) { return absent.contains(device.name); });
        };
        unplug(shown.inputs);
        unplug(shown.outputs);
        // **A default that is not there is not a default.** WirePlumber
        // moves the default on when a device goes away, and a block that
        // follows the default has to follow it somewhere.
        if (absent.contains(shown.defaultSink)) {
            shown.defaultSink.clear();
        }
        if (absent.contains(shown.defaultSource)) {
            shown.defaultSource.clear();
        }
        inventory = pw::inventoryToJson(shown);
        Q_EMIT q->inventoryChanged(asJson(inventory));
    }

    /// **Pretend Recorders**: a file that starts when the session
    /// does, named as the engine would name it, so an interface built
    /// against the mock shows a recording going. Nothing is written.
    /// **What a Broadcast block's stream would be doing**: on the air
    /// while its session runs and it has a password, waiting for one while
    /// it runs without, and idle otherwise. The numbers are the ones a
    /// stream has just after it connects.
    void announceBroadcast(const Open &session, const QString &sessionId, const QString &blockId)
    {
        const BlockInstance *block = session.session.findBlock(blockId);
        const bool stored = broadcastPasswords.contains(sessionId + u'/' + blockId);
        stream::BroadcastStatus status;
        if (session.state == sessionState::running && block != nullptr && block->enabled) {
            if (stored) {
                status.state = stream::BroadcastStatus::State::Connected;
                status.connectedSinceMs = QDateTime::currentMSecsSinceEpoch();
                status.sampleRate = 48000;
            } else {
                status.state = stream::BroadcastStatus::State::Failed;
                status.error = stream::StreamError::NoPassword;
            }
        }
        QJsonObject json = broadcastStatusJson(status);
        json.insert(u"passwordStored"_s, stored);
        Q_EMIT q->broadcastStatusChanged(sessionId, blockId, asJson(json));
    }

    [[nodiscard]] static QList<const BlockInstance *> recorders(const Session &session)
    {
        QList<const BlockInstance *> found;
        for (const BlockInstance &block : session.blocks) {
            if (block.type == "output.recorder"_L1 && block.enabled) {
                found.append(&block);
            }
        }
        return found;
    }

    [[nodiscard]] static QString pretendFile(const Session &session, const BlockInstance &block)
    {
        QString folder = block.params.value("folder"_L1).toString();
        if (folder.trimmed().isEmpty()) {
            folder = defaultRecordingFolderPath();
        }
        const QString format = block.params.value("format"_L1).toString(u"mp3"_s);
        return folder + u'/'
            + recordingFileNamePreview(block.params.value("fileName"_L1).toString(),
                                       session.name,
                                       block.nickname.isEmpty() ? block.id : block.nickname,
                                       format);
    }

    void fail(ErrorCode code, const QStringList &arguments, const QString &blockId = {})
    {
        const ErrorCodeInfo &info = kErrorCodes[static_cast<std::size_t>(code)];
        QJsonObject args;
        if (!blockId.isEmpty()) {
            args.insert(u"block"_s, blockId);
        }
        for (qsizetype index = 0; index < arguments.size() && index < static_cast<qsizetype>(info.arguments.size());
             ++index) {
            args.insert(asText(info.arguments[static_cast<std::size_t>(index)]), arguments.at(index));
        }
        Q_EMIT q->error(asText(info.name), asJson(args));
    }

    /// What the pretend audio system would say about itself. Versions that
    /// look like versions, because an interface that lays them out has to
    /// have something the length of the real thing to lay out.
    [[nodiscard]] AudioSystemInfo audioSystem() const
    {
        AudioSystemInfo info;
        if (!attached) {
            // Nothing is known about a server that is not there, and a
            // version remembered from before it went is not knowledge.
            info.state = QString(audioSystemState::waiting);
            return info;
        }
        info.state = QString(audioSystemState::connected);
        info.serverName = u"PipeWire"_s;
        info.serverVersion = u"1.4.2"_s;
        info.sessionManagerName = u"WirePlumber"_s;
        info.sessionManagerVersion = u"0.5.8"_s;
        info.driver = u"Built-in Audio Analogue Stereo"_s;
        info.rate = 48000;
        info.quantum = 1024;
        return info;
    }

    void sendAudioSystem()
    {
        Q_EMIT q->audioSystemChanged(audioSystemToJson(audioSystem()));
    }

    [[nodiscard]] Open *require(const QString &sessionId)
    {
        const auto found = open.find(sessionId);
        if (found == open.end()) {
            fail(ErrorCode::SessionNotFound, {sessionId});
            return nullptr;
        }
        return &found->second;
    }

    void sendSnapshot(const Open &session)
    {
        Q_EMIT q->sessionSnapshot(session.session.id.toString(QUuid::WithoutBraces),
                                  session.session.revision,
                                  asJson(sessionToJson(session.session)));
    }

    void setState(Open &session, QLatin1StringView state)
    {
        const bool wasRunning = session.state == sessionState::running;
        if (state == sessionState::running && !wasRunning) {
            session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
        } else if (state != sessionState::running) {
            session.startedAtMs = 0;
        }
        session.state = QString(state);
        if (wasRunning && state != sessionState::running) {
            publish(session, false);
        }
        followRunningSessions();
        Q_EMIT q->stateChanged(session.session.id.toString(QUuid::WithoutBraces), session.state);
    }

    void announceUndo(Open &session, bool always = false)
    {
        const UndoState now = UndoState::of(session.history);
        if (!always && now == session.announced) {
            return;
        }
        session.announced = now;
        Q_EMIT q->undoStateChanged(
            session.session.id.toString(QUuid::WithoutBraces), now.canUndo, now.undoCode, now.canRedo, now.redoCode);
    }

    /// Applies a step's commands and sends a patch for each, as the engine
    /// does. A step that will not apply takes the history with it.
    void applyStep(Open &session, const std::vector<EditCommand> &commands)
    {
        const QString id = session.session.id.toString(QUuid::WithoutBraces);
        for (const EditCommand &command : commands) {
            const std::expected<EditInverse, EditError> applied = core::applyEdit(session.session, command);
            if (!applied) {
                fail(ErrorCode::EditRefused, {asText(editErrorName(applied.error()))});
                session.history.clear();
                return;
            }
            Q_EMIT q->sessionPatch(id, session.session.revision, asJson(editCommandToJson(command)));
        }
    }
};

MockEngine::MockEngine(PresetStore presets, QObject *parent)
    : EngineFacade(parent)
    , d(std::make_unique<Private>(this, std::move(presets)))
{
    d->inventory = pw::inventoryToJson(pw::Inventory{});
    d->resetFakePlugins();
    // **What a plugin block asks about its plugin**, answered from the
    // fake plugins, as the engine answers from its cache.
    plugin::setPluginInfoSource(
        [this](std::string_view format, std::string_view id) -> std::optional<plugin::PluginDescription> {
            for (const InstalledPlugin &fake : d->fakePlugins) {
                if (fake.format.toStdString() == format && fake.id.toStdString() == id) {
                    plugin::PluginDescription description;
                    description.format = std::string{format};
                    description.id = std::string{id};
                    description.name = fake.name.toStdString();
                    description.vendor = fake.vendor.toStdString();
                    description.audioInputs = static_cast<std::uint32_t>(fake.audioInputs);
                    description.audioOutputs = static_cast<std::uint32_t>(fake.audioOutputs);
                    description.params = paramsFromJson(fake.params);
                    description.parameters = static_cast<std::uint32_t>(description.params.size());
                    return description;
                }
            }
            return std::nullopt;
        });
    connect(&d->library, &RecordingLibrary::changed, this, [this](const QString &sessionId) {
        Q_EMIT recordingsChanged(sessionId, asJson(d->library.recordings(sessionId)));
    });
    d->timers = std::make_unique<TimerRunner>(
        this,
        [this] { return sessionListing(); },
        [this](const QString &sessionId) { d->applicationsQuitFor.append(sessionId); });
    connect(d->timers.get(), &TimerRunner::fired, this, &EngineFacade::timerFired);
    connect(d->timers.get(), &TimerRunner::notFound, this, [this](const QString &timerId) {
        d->fail(ErrorCode::TimerNotFound, {timerId});
    });
    connect(d->timers.get(), &TimerRunner::late, this, [this](const QString &timerId) {
        d->fail(ErrorCode::TimerLate, {timerId});
    });
    d->preview.tick.setInterval(100);
    connect(&d->preview.tick, &QTimer::timeout, this, [this] {
        if (d->previewSeconds() >= d->preview.duration) {
            d->preview.heldSeconds = d->preview.duration;
            d->setPreviewState(u"stopped"_s);
            return;
        }
        d->announcePreview();
    });
}

MockEngine::~MockEngine()
{
    // Nobody left to answer: a plugin block made after this knows no plugin.
    plugin::setPluginInfoSource({});
}

void MockEngine::addFakeInventory()
{
    const QString sink = u"alsa_output.analog"_s;
    const QString source = u"alsa_input.usb-Microphone"_s;
    const QStringList stereo{u"FL"_s, u"FR"_s};

    pw::Inventory made;
    made.applications = {application(u"firefox"_s, u"Firefox"_s, sink),
                         application(u"musicplayer"_s, u"Music Player"_s, sink)};
    // A mono microphone and a surround card, because a picker that has only
    // ever been shown stereo pairs has never had its channel lists read.
    made.inputs = {device(source, u"USB Microphone"_s, true, {u"MONO"_s})};
    made.outputs = {
        device(u"alsa_output.hdmi"_s, u"HDMI"_s, false, {u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s}),
        device(sink, u"Headphones"_s, true, stereo),
    };
    made.defaultSink = sink;
    made.defaultSource = source;
    made.configuredSink = sink;
    made.configuredSource = source;
    d->made = std::move(made);
    d->absent.clear();
    d->publishInventory();
}

void MockEngine::addFakeHeadset(bool playbackProfile)
{
    // Named as WirePlumber names a headset's loopback source and its sink.
    const QString microphoneName = u"bluez_input.00:1B:66:A1:B2:C3"_s;
    const QString earpiecesName = u"bluez_output.00_1B_66_A1_B2_C3.1"_s;
    d->made.inputs.removeIf([&](const pw::InventoryDevice &device) { return device.name == microphoneName; });
    d->made.outputs.removeIf([&](const pw::InventoryDevice &device) { return device.name == earpiecesName; });

    pw::InventoryDevice microphone = device(microphoneName, u"Wireless Headset"_s, false, {u"MONO"_s});
    microphone.mediaClass = u"Audio/Source"_s;
    microphone.isWireless = true;
    microphone.isOnPlaybackProfile = playbackProfile;
    // Stereo while it only plays, one channel each way once it is a headset.
    pw::InventoryDevice earpieces = device(earpiecesName,
                                           u"Wireless Headset"_s,
                                           false,
                                           playbackProfile ? QStringList{u"FL"_s, u"FR"_s} : QStringList{u"MONO"_s});
    earpieces.mediaClass = u"Audio/Sink"_s;
    earpieces.isWireless = true;

    d->made.inputs.append(microphone);
    d->made.outputs.append(earpieces);
    d->publishInventory();
}

void MockEngine::addFakeVirtualDevices()
{
    const QStringList stereo{u"FL"_s, u"FR"_s};
    pw::InventoryDevice everyApp = device(u"virtual.everyapp"_s, u"Every app"_s, false, stereo);
    everyApp.mediaClass = u"Audio/Sink"_s;
    everyApp.isVirtual = true;
    pw::InventoryDevice monitor = everyApp;
    monitor.isMonitor = true;
    pw::InventoryDevice microphone = device(u"virtual.processed"_s, u"Processed microphone"_s, false, stereo);
    microphone.mediaClass = u"Audio/Source/Virtual"_s;
    microphone.isVirtual = true;

    d->made.outputs.append(everyApp);
    d->made.outputs.append(microphone);
    d->made.inputs.append(monitor);
    d->made.inputs.append(microphone);
    d->publishInventory();
}

void MockEngine::setFakeDevicePresent(const QString &node, bool present)
{
    if (present) {
        d->absent.removeAll(node);
    } else if (!d->absent.contains(node)) {
        d->absent.append(node);
    }
    d->publishInventory();
}

void MockEngine::listSessions()
{
    Q_EMIT sessionsListed(sessionListing());
}

QString MockEngine::sessionListing() const
{
    QJsonArray sessions;
    for (const auto &[id, session] : d->open) {
        // Nothing is on disk, so every session it lists is open.
        sessions.append(
            sessionListEntry(session.session, session.state, d->library.recordingCount(id), session.startedAtMs));
    }
    return QString::fromUtf8(QJsonDocument(sessions).toJson(QJsonDocument::Compact));
}

QStringList MockEngine::applicationsQuitFor() const
{
    return d->applicationsQuitFor;
}

void MockEngine::listTimers()
{
    Q_EMIT timersListed(d->timers->timerListing(QDateTime::currentDateTime()));
}

void MockEngine::fireTimer(const QString &timerId, bool scheduled)
{
    d->timers->fire(timerId, scheduled);
}

void MockEngine::createSession(const QString &name)
{
    Private::Open session;
    session.session.id = QUuid::createUuid();
    session.session.name = name;
    const QString id = session.session.id.toString(QUuid::WithoutBraces);
    d->open.emplace(id, std::move(session));
    Q_EMIT sessionOpened(id);
    d->sendSnapshot(d->open.at(id));
}

void MockEngine::openSession(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        Q_EMIT sessionOpened(sessionId);
        d->sendSnapshot(*session);
    }
}

void MockEngine::saveSession(const QString &sessionId)
{
    // Nothing is on disk, so saving is agreeing that it happened.
    (void)d->require(sessionId);
}

void MockEngine::closeSession(const QString &sessionId)
{
    if (d->require(sessionId) != nullptr) {
        d->open.erase(sessionId);
        Q_EMIT sessionClosed(sessionId);
    }
}

void MockEngine::deleteSession(const QString &sessionId)
{
    closeSession(sessionId);
}

void MockEngine::exportSession(const QString &sessionId, const QString &path)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **A real file, even from the mock.** The mock keeps its sessions in a
    // map rather than on disk, but an export is a file somebody asked for and
    // then goes looking at — so this writes one, through the same atomic
    // write the engine uses, and a test can read it back.
    const SessionStore store;
    if (const std::expected<void, SessionError> saved = store.saveFile(session->session, path); !saved) {
        d->fail(ErrorCode::SessionNotSaved, {session->session.name, asText(sessionErrorName(saved.error()))});
    }
}

void MockEngine::importSession(const QString &path)
{
    const SessionStore store;
    std::expected<Session, SessionLoadFailure> loaded =
        store.loadFile(path, [this](QStringView type) { return d->registry.contains(type.toString().toStdString()); });
    if (!loaded) {
        if (!loaded.error().backupPath.isEmpty()) {
            d->fail(ErrorCode::SessionCorruptFile, {path, loaded.error().backupPath});
        } else {
            d->fail(ErrorCode::SessionNotFound, {path});
        }
        return;
    }
    // **An import is a copy**, the same rule the engine follows: a new id, so
    // that importing a session somebody sent cannot overwrite one of your own
    // whose id happens to match, and importing the same file twice gives two
    // sessions.
    Private::Open session;
    session.session = std::move(*loaded);
    session.session.id = QUuid::createUuid();
    session.session.revision = 0;
    session.session.timers = timersWithNewIds(session.session.timers);
    const QString id = session.session.id.toString(QUuid::WithoutBraces);
    for (const BlockInstance &block : session.session.blocks) {
        if (block.unknownType) {
            d->fail(ErrorCode::BlockUnknownType, {block.type});
        }
    }
    d->open.emplace(id, std::move(session));
    Q_EMIT sessionOpened(id);
    d->sendSnapshot(d->open.at(id));
}

void MockEngine::listPresets(const QString &typeId)
{
    Q_EMIT presetsListed(typeId, asJson(presetsToJson(d->presets.presetsFor(typeId))));
}

void MockEngine::applyPreset(const QString &sessionId, const QString &blockId, const QString &presetId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const BlockInstance *block = session->session.findBlock(blockId);
    if (block == nullptr) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(PresetError::NotFound))});
        return;
    }
    const std::expected<Preset, PresetError> preset = d->presets.preset(block->type, presetId);
    if (!preset) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(preset.error()))});
        return;
    }
    const std::expected<ApplyPreset, PresetError> edit =
        applyPresetEdit(d->registry, session->session, blockId, *preset);
    if (!edit) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(edit.error()))});
        return;
    }
    // **Through applyEdit, like anything else.** It is one edit, so it is
    // one step to undo, it is numbered like every other change, and the
    // patch it sends is what a second client follows.
    applyEdit(sessionId, asJson(editCommandToJson(*edit)));
}

void MockEngine::savePreset(const QString &sessionId, const QString &blockId, const QString &name, bool replace)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const std::expected<Preset, PresetError> preset = presetOfBlock(d->registry, session->session, blockId, name);
    if (!preset) {
        d->fail(ErrorCode::PresetRefused, {name, asText(presetErrorName(preset.error()))});
        return;
    }
    const std::expected<Preset, PresetError> saved = d->presets.save(*preset, replace);
    if (!saved) {
        d->fail(ErrorCode::PresetRefused, {name, asText(presetErrorName(saved.error()))});
        return;
    }
    // **The block is left showing what it was just saved as.** Somebody who
    // saves the settings they are listening to expects the menu to say so
    // rather than "Manual".
    ApplyPreset marker;
    marker.blockId = blockId;
    marker.presetId = saved->id;
    marker.params = saved->params;
    applyEdit(sessionId, asJson(editCommandToJson(marker)));
    listPresets(preset->typeId);
}

void MockEngine::renamePreset(const QString &typeId, const QString &presetId, const QString &name)
{
    if (const std::expected<Preset, PresetError> renamed = d->presets.rename(typeId, presetId, name); !renamed) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(renamed.error()))});
        return;
    }
    listPresets(typeId);
}

void MockEngine::deletePreset(const QString &typeId, const QString &presetId)
{
    if (const std::expected<void, PresetError> removed = d->presets.remove(typeId, presetId); !removed) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(removed.error()))});
        return;
    }
    listPresets(typeId);
}

void MockEngine::applyEdit(const QString &sessionId, const QString &editJson)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(editJson.toUtf8());
    const std::expected<std::vector<EditCommand>, EditJsonError> commands = editCommandsFromJson(document);
    if (!commands) {
        d->fail(ErrorCode::EditRefused, {asText(editJsonErrorName(commands.error()))});
        return;
    }
    // **Ports are checked where the registry is** (blockcatalogue.h): the
    // session alone cannot say how many ports a block type has, so a wire
    // to a port that does not exist would otherwise be taken here and
    // refused by the graph compiler when somebody pressed Start.
    for (const EditCommand &command : *commands) {
        if (const auto *connect = std::get_if<Connect>(&command); connect != nullptr) {
            if (const QString refused = wireRefusal(session->session, d->registry, connect->wire); !refused.isEmpty()) {
                d->fail(ErrorCode::EditRefused, {refused});
                return;
            }
        }
        // **A timer as a client writes it is checked**: putting one back
        // unread is undo's, not a caller's, and one id is one systemd
        // timer, so no two sessions may have it.
        if (const auto *timer = std::get_if<SetTimer>(&command); timer != nullptr) {
            if (timer->raw) {
                d->fail(ErrorCode::EditRefused, {asText(editErrorName(EditError::TimerNotValid))});
                return;
            }
            if (timerIdTakenElsewhere(sessionListing(), sessionId, timer->timer.value("id"_L1).toString())) {
                d->fail(ErrorCode::EditRefused, {u"timerIdInUse"_s});
                return;
            }
        }
    }
    // The real model, not a pretend one: an edit the engine would refuse has
    // to be refused here too, or an interface written against the mock finds
    // out the hard way — and a list applies whole or not at all here for the
    // same reason.
    const std::uint64_t startedAt = session->session.revision;
    Session trial = session->session;
    const std::expected<EditInverse, EditBatchError> applied = applyEdits(trial, *commands);
    if (!applied) {
        d->fail(ErrorCode::EditRefused, {asText(editErrorName(applied.error().error))});
        return;
    }
    session->session = std::move(trial);
    session->history.record(*commands, *applied);
    for (std::size_t index = 0; index < commands->size(); ++index) {
        Q_EMIT sessionPatch(sessionId, startedAt + index + 1, asJson(editCommandToJson((*commands)[index])));
    }
    d->announceUndo(*session);
}

void MockEngine::setParam(const QString &sessionId,
                          const QString &blockId,
                          const QString &paramId,
                          const QString &valueJson)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **The same rule as the engine's**, or an interface written against
    // the mock finds out the hard way. Qt will not parse a bare `true` or a
    // bare string as a document, so a single value arrives wrapped in an
    // array and is unwrapped here.
    // **Parsed, and refused when it will not parse.** A document that is not
    // an array is not a value the interface sent: `QJsonValue::fromVariant()`
    // of a variant that holds nothing is **null**, not undefined, so reading
    // it that way took an unparseable value for a deliberate null and wrote
    // it into the session. Found while testing autosave.
    QJsonParseError parsed{};
    const QJsonDocument document = QJsonDocument::fromJson(valueJson.toUtf8(), &parsed);
    const QJsonValue value = parsed.error == QJsonParseError::NoError && document.isArray()
        ? document.array().at(0)
        : QJsonValue{QJsonValue::Undefined};
    if (value.isUndefined()) {
        d->fail(ErrorCode::EditRefused, {u"malformedValue"_s});
        return;
    }
    for (BlockInstance &block : session->session.blocks) {
        if (block.id == blockId) {
            const QJsonValue previous = block.params.value(paramId);
            block.params.insert(paramId, value);
            session->history.record(SetParam{blockId, paramId, value},
                                    EditInverse{SetParam{blockId, paramId, previous}});
            d->announceUndo(*session);
            return;
        }
    }
}

void MockEngine::undo(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (const std::optional<EditStep> step = session->history.takeUndo()) {
        d->applyStep(*session, step->undo);
        d->announceUndo(*session);
    }
}

void MockEngine::redo(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (const std::optional<EditStep> step = session->history.takeRedo()) {
        d->applyStep(*session, step->redo);
        d->announceUndo(*session);
    }
}

void MockEngine::getUndoState(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        d->announceUndo(*session, true);
    }
}

void MockEngine::restartPlugin(const QString &sessionId, const QString &blockId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **The engine's refusals, in the engine's order.** A plugin the mock
    // has is never running anywhere, so there is nothing more to do.
    const BlockInstance *block = session->session.findBlock(blockId);
    QString refused;
    if (session->state != sessionState::running) {
        refused = u"NotRunning"_s;
    } else if (block == nullptr) {
        refused = u"blockNotFound"_s;
    } else if (!block->type.startsWith("plugin."_L1) || !block->enabled) {
        refused = u"notAHostedPlugin"_s;
    }
    if (!refused.isEmpty()) {
        d->fail(ErrorCode::PluginRestartRefused, {refused}, blockId);
    }
}

namespace
{

/// Why the engine would refuse to ask \a blockId's host anything, in the
/// engine's words, or empty when it would ask.
[[nodiscard]] QString hostRefusal(const QString &state, const BlockInstance *block)
{
    if (state != sessionState::running) {
        return u"NotRunning"_s;
    }
    if (block == nullptr) {
        return u"blockNotFound"_s;
    }
    if (!block->type.startsWith("plugin."_L1) || !block->enabled) {
        return u"notAHostedPlugin"_s;
    }
    return {};
}

} // namespace

void MockEngine::setPluginWindow(const QString &sessionId, const QString &blockId, bool open)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **A window the mock has none of is said to show**, so an interface
    // can follow the answer as it would the engine's.
    const QString refused = hostRefusal(session->state, session->session.findBlock(blockId));
    if (!refused.isEmpty()) {
        Q_EMIT pluginWindowChanged(
            sessionId, blockId, false, refused == "NotRunning"_L1 ? u"notRunning"_s : u"unavailable"_s);
        return;
    }
    Q_EMIT pluginWindowChanged(sessionId, blockId, open, open ? QString{} : u"closed"_s);
}

void MockEngine::listPluginPresets(const QString &sessionId, const QString &blockId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (const QString refused = hostRefusal(session->state, session->session.findBlock(blockId)); !refused.isEmpty()) {
        d->fail(ErrorCode::PluginPresetRefused, {QString{}, refused}, blockId);
        return;
    }
    Q_EMIT pluginPresetsListed(sessionId, blockId, asJson(QJsonArray{}));
}

void MockEngine::loadPluginPreset(const QString &sessionId, const QString &blockId, const QString &presetId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (const QString refused = hostRefusal(session->state, session->session.findBlock(blockId)); !refused.isEmpty()) {
        d->fail(ErrorCode::PluginPresetRefused, {presetId, refused}, blockId);
        return;
    }
    Q_EMIT pluginPresetShown(sessionId, blockId, presetId);
}

void MockEngine::savePluginPreset(const QString &sessionId, const QString &blockId, const QString &name)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (const QString refused = hostRefusal(session->state, session->session.findBlock(blockId)); !refused.isEmpty()) {
        d->fail(ErrorCode::PluginPresetRefused, {name, refused}, blockId);
        return;
    }
    const QString id = u"mock:"_s + name;
    Q_EMIT pluginPresetsListed(sessionId, blockId, asJson(QJsonArray{QJsonObject{{u"id"_s, id}, {u"name"_s, name}}}));
    Q_EMIT pluginPresetShown(sessionId, blockId, id);
}

void MockEngine::controlRecording(const QString &sessionId, const QString &blockId, const QString &action)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **The engine's refusals, in the engine's order**, so an interface
    // built against the mock handles the same answers.
    const BlockInstance *block = session->session.findBlock(blockId);
    QString refused;
    if (session->state != sessionState::running) {
        refused = u"NotRunning"_s;
    } else if (block == nullptr) {
        refused = u"blockNotFound"_s;
    } else if (block->type != QLatin1StringView{"output.recorder"}) {
        refused = u"notARecorder"_s;
    } else if (action != "pause"_L1 && action != "resume"_L1 && action != "split"_L1) {
        refused = u"unknownAction"_s;
    }
    if (!refused.isEmpty()) {
        d->fail(ErrorCode::RecordingRefused, {refused}, blockId);
        return;
    }
    if (action != "split"_L1) {
        Q_EMIT recordingPaused(sessionId, blockId, action == "pause"_L1);
        return;
    }
    const QString file = Private::pretendFile(session->session, *block);
    Q_EMIT recordingFileFinished(sessionId, blockId, file, u"{\"frames\":0,\"sampleRate\":48000,\"seconds\":0}"_s);
    Q_EMIT recordingFileStarted(sessionId, blockId, file);
}

void MockEngine::setBroadcastPassword(const QString &sessionId, const QString &blockId, const QString &password)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const BlockInstance *block = session->session.findBlock(blockId);
    if (block == nullptr || !isStreamingType(block->type.toStdString())) {
        d->fail(ErrorCode::BroadcastPasswordNotSaved, {u"notABroadcast"_s}, blockId);
        return;
    }
    const QString key = sessionId + u'/' + blockId;
    if (password.isEmpty()) {
        d->broadcastPasswords.remove(key);
    } else {
        d->broadcastPasswords.insert(key, password);
    }
    d->announceBroadcast(*session, sessionId, blockId);
}

void MockEngine::getBroadcastStatus(const QString &sessionId, const QString &blockId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        d->announceBroadcast(*session, sessionId, blockId);
    }
}

void MockEngine::start(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const bool wasRunning = session->state == sessionState::running;
    if (!d->attached) {
        // **The same refusal the engine makes**, and for the same reason:
        // there is nothing to put a node into. The reason is a code, as the
        // engine's is, and the interface holds the words for it.
        d->setState(*session, sessionState::starting);
        d->fail(ErrorCode::SessionCannotRun, {session->session.name, u"audioSystemUnavailable"_s});
        d->setState(*session, sessionState::error);
        return;
    }
    d->setState(*session, sessionState::starting);
    d->setState(*session, sessionState::running);
    // The same rule as the engine's, so that an interface built against the
    // mock behaves the same way against the real thing.
    if (!wasRunning) {
        for (const BlockInstance *recorder : Private::recorders(session->session)) {
            Q_EMIT recordingFileStarted(sessionId, recorder->id, Private::pretendFile(session->session, *recorder));
        }
        for (const BlockInstance &block : session->session.blocks) {
            if (isStreamingType(block.type.toStdString())) {
                d->announceBroadcast(*session, sessionId, block.id);
            }
        }
        for (const QString &url : urlsToOpen(session->session)) {
            Q_EMIT openUrlRequested(sessionId, url);
        }
    }
}

void MockEngine::stop(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        const bool wasRunning = session->state == sessionState::running;
        d->setState(*session, sessionState::stopping);
        // Every file finished before the session is idle, as the engine does.
        if (wasRunning) {
            for (const BlockInstance *recorder : Private::recorders(session->session)) {
                Q_EMIT recordingFileFinished(sessionId,
                                             recorder->id,
                                             Private::pretendFile(session->session, *recorder),
                                             u"{\"frames\":0,\"sampleRate\":48000,\"seconds\":0}"_s);
            }
        }
        d->setState(*session, sessionState::idle);
        for (const BlockInstance &block : session->session.blocks) {
            if (isStreamingType(block.type.toStdString())) {
                d->announceBroadcast(*session, sessionId, block.id);
            }
        }
    }
}

void MockEngine::getSessionState(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        Q_EMIT stateChanged(sessionId, session->state);
    }
}

void MockEngine::resynchronise(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        d->sendSnapshot(*session);
    }
}

void MockEngine::getInventory()
{
    Q_EMIT inventoryChanged(asJson(d->inventory));
}

const TelemetrySlot *MockEngine::telemetryOf(const QString &sessionId) const
{
    const auto found = d->open.find(sessionId);
    return found == d->open.end() ? nullptr : &found->second.telemetry.slot();
}

int MockEngine::shareTelemetry(const QString &sessionId) const
{
    const auto found = d->open.find(sessionId);
    return found == d->open.end() ? -1 : found->second.telemetry.shareReadOnly();
}

void MockEngine::setFakeLevel(const QString &sessionId, const QString &blockId, double peak)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    session->levels.insert(blockId, static_cast<float>(std::clamp(peak, 0.0, 1.0)));
    // **Sent at once, not on the next tick.** A test that asked for a
    // signal and then waited for a colour would be waiting on the timer as
    // well as on the thing it is testing, and the hundred milliseconds it
    // is allowed to take would be mostly spent before anything happened.
    if (session->state == sessionState::running) {
        d->publish(*session, true);
    }
}

void MockEngine::setFakeSpectrum(const QString &sessionId, const QString &blockId, const QVariantList &bandsDb)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    session->spectra.insert(blockId, bandsDb);
    if (session->state == sessionState::running) {
        d->publish(*session, true);
    }
}

void MockEngine::setFakeInputSpectrum(const QString &sessionId, const QString &blockId, const QVariantList &bandsDb)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    session->inputSpectra.insert(blockId, bandsDb);
    if (session->state == sessionState::running) {
        d->publish(*session, true);
    }
}

void MockEngine::setFakeBandValues(const QString &sessionId, const QString &blockId, const QVariantList &values)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    session->bandValues.insert(blockId, values);
    if (session->state == sessionState::running) {
        d->publish(*session, true);
    }
}

QString MockEngine::analysed(const QString &sessionId) const
{
    const auto found = d->open.find(sessionId);
    return found == d->open.end() ? QString{} : found->second.analysed;
}

int MockEngine::analysedChannel(const QString &sessionId) const
{
    const auto found = d->open.find(sessionId);
    return found == d->open.end() ? kBothChannels : found->second.analysedChannel;
}

void MockEngine::analyse(const QString &sessionId, const QString &blockId, int channel)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    session->analysed = blockId;
    session->analysedChannel = channel == 0 || channel == 1 ? channel : kBothChannels;
    if (session->state == sessionState::running) {
        d->publish(*session, true);
    }
}

void MockEngine::clearFakeLevels(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr || session->levels.isEmpty()) {
        return;
    }
    session->levels.clear();
    if (session->state == sessionState::running) {
        d->publish(*session, true);
    }
}

void MockEngine::setAudioSystemRunning(bool running)
{
    if (d->audioSystemRunning == running) {
        return;
    }
    d->audioSystemRunning = running;
    if (!running) {
        // A server that goes away takes the connection and every running
        // session with it. **Coming back does not undo that**: the server
        // being there again is not a connection to it, and nothing knows
        // there is one until something asks — which is what Retry does.
        d->attached = false;
        for (auto &[id, session] : d->open) {
            if (session.state == sessionState::running || session.state == sessionState::starting) {
                d->setState(session, sessionState::idle);
            }
        }
        d->sendAudioSystem();
    }
}

bool MockEngine::isAudioSystemRunning() const
{
    return d->audioSystemRunning;
}

void MockEngine::reportError(const QString &code, const QString &argsJson)
{
    Q_EMIT error(code, argsJson);
}

void MockEngine::getAudioSystem()
{
    d->sendAudioSystem();
}

void MockEngine::reconnectAudioSystem()
{
    d->attached = d->audioSystemRunning;
    // **Trying is not succeeding.** The answer goes out either way, so that
    // a Retry pressed while the server is still away says so rather than
    // leaving the interface waiting for a signal that never comes.
    d->sendAudioSystem();
}

void MockEngine::getDiagnostics()
{
    QJsonArray sessions;
    for (const auto &[id, open] : d->open) {
        sessions.append(QJsonObject{
            {"id"_L1, id},
            {"name"_L1, open.session.name},
            {"state"_L1, open.state},
            {"running"_L1, open.state == sessionState::running},
            {"xrunHistory"_L1, QJsonArray{}},
        });
    }
    QJsonObject answer{
        {"version"_L1, kDiagnosticsVersion},
        {"engine"_L1,
         QJsonObject{
             {"process"_L1, QCoreApplication::applicationName()},
             {"version"_L1, QString::fromUtf8(rb::versionFull())},
             {"pid"_L1, QCoreApplication::applicationPid()},
             {"pipeWireHeaders"_L1, u"mock"_s},
             {"pipeWireLibrary"_L1, u"mock"_s},
             {"logKept"_L1, LogRing::isInstalled()},
         }},
        {"audioSystem"_L1, QJsonDocument::fromJson(audioSystemToJson(d->audioSystem()).toUtf8()).object()},
        {"sessions"_L1, sessions},
        {"graph"_L1, QJsonObject{{"nodes"_L1, QJsonArray{}}, {"ports"_L1, QJsonArray{}}, {"links"_L1, QJsonArray{}}}},
        {"recordingFormats"_L1, recordingFormatsJson()},
        {"plugins"_L1, pluginScanJson(d->fakePlugins, d->fakeBlocked, false, d->fakeIncomplete)},
        {"log"_L1, logJson()},
    };
    for (auto it = d->fakeDiagnostics.constBegin(); it != d->fakeDiagnostics.constEnd(); ++it) {
        answer.insert(it.key(), it.value());
    }
    Q_EMIT diagnostics(asJson(answer));
}

void MockEngine::setFakeDiagnostics(const QString &diagnosticsJson)
{
    d->fakeDiagnostics = QJsonDocument::fromJson(diagnosticsJson.toUtf8()).object();
}

void MockEngine::addBlockTypeForTesting(const std::string &typeId, std::unique_ptr<dsp::Block> (*factory)())
{
    (void)d->registry.add(typeId, factory);
    getBlockCatalogue();
}

void MockEngine::getBlockCatalogue()
{
    // **The real catalogue, from the real blocks.** The mock exists so that
    // interface work can start before the engine is finished; a made-up list
    // of parameters would be the one thing an interface cannot be built
    // against, because the panel is generated from exactly these.
    UnavailableChoices unavailable = recordingUnavailableChoices();
    unavailable.merge(broadcastUnavailableChoices());
#ifdef RB_HAVE_LIVESTREAM
    unavailable.merge(liveStreamUnavailableChoices());
    // **The graphics cards are asked on a thread of their own**, the first
    // time: until they have answered, the catalogue offers the software
    // encoders alone, and it is said again once they have.
    (void)probeVideoEncodersFor(this, [this] { getBlockCatalogue(); });
#endif
    Q_EMIT blockCatalogueChanged(asJson(blockCatalogueJson(d->registry, unavailable)));
}

namespace
{

[[nodiscard]] std::optional<pw::VirtualDeviceStatus> virtualDeviceStatusFrom(const QString &name)
{
    for (const pw::VirtualDeviceStatus status : {pw::VirtualDeviceStatus::Disabled,
                                                 pw::VirtualDeviceStatus::Present,
                                                 pw::VirtualDeviceStatus::Creating,
                                                 pw::VirtualDeviceStatus::Missing,
                                                 pw::VirtualDeviceStatus::WaitingForPipeWire}) {
        if (name == asText(pw::virtualDeviceStatusName(status))) {
            return status;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<pw::VirtualDeviceScope> virtualDeviceScopeFrom(const QString &name)
{
    for (const pw::VirtualDeviceScope scope : {pw::VirtualDeviceScope::User, pw::VirtualDeviceScope::System}) {
        if (name == pw::virtualDeviceScopeName(scope)) {
            return scope;
        }
    }
    return std::nullopt;
}

} // namespace

QString MockEngine::addFakeVirtualDevice(const QString &deviceJson)
{
    QJsonObject object = QJsonDocument::fromJson(deviceJson.toUtf8()).object();
    if (!object.contains("direction"_L1)) {
        object.insert("direction"_L1, u"output"_s);
    }
    const std::expected<VirtualDeviceRequest, QString> request =
        virtualDeviceRequestFromJson(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    pw::VirtualDeviceState state;
    state.device.id = pw::newVirtualDeviceId(d->allVirtualDevices());
    state.device.name = object.value("name"_L1).toString().trimmed();
    if (request) {
        state.device.direction = request->direction;
        state.device.channels = request->channels;
    }
    state.device.enabled = object.value("enabled"_L1).toBool(true);
    state.device.scope =
        virtualDeviceScopeFrom(object.value("scope"_L1).toString()).value_or(pw::VirtualDeviceScope::User);
    state.status =
        virtualDeviceStatusFrom(object.value("status"_L1).toString())
            .value_or(state.device.enabled ? pw::VirtualDeviceStatus::Present : pw::VirtualDeviceStatus::Disabled);
    // The user's own first, as the real manager lists them.
    if (state.device.scope == pw::VirtualDeviceScope::User) {
        const auto firstSystem = std::ranges::find_if(d->virtualDevices, [](const pw::VirtualDeviceState &listed) {
            return listed.device.scope == pw::VirtualDeviceScope::System;
        });
        d->virtualDevices.insert(firstSystem, state);
    } else {
        d->virtualDevices.append(state);
    }
    d->publishVirtualDevices();
    d->publishInventory();
    return state.device.id;
}

void MockEngine::setFakeVirtualDeviceStatus(const QString &deviceId, const QString &status)
{
    const std::optional<pw::VirtualDeviceStatus> known = virtualDeviceStatusFrom(status);
    pw::VirtualDeviceState *state = d->findVirtualDevice(deviceId);
    if (!known || state == nullptr || state->status == *known) {
        return;
    }
    state->status = *known;
    d->publishVirtualDevices();
    d->publishInventory();
}

void MockEngine::refuseNextVirtualDeviceChange(const QString &reason)
{
    d->refuseNext = virtualDeviceErrorFromReason(reason);
}

void MockEngine::setFakeVirtualDeviceFileUnreadable(const QString &scope, const QString &reason)
{
    const std::optional<pw::VirtualDeviceScope> which = virtualDeviceScopeFrom(scope);
    if (!which) {
        return;
    }
    if (const std::optional<pw::VirtualDeviceError> error = virtualDeviceErrorFromReason(reason)) {
        d->unreadable[*which] = *error;
    } else {
        d->unreadable.erase(*which);
    }
    d->publishVirtualDevices();
    d->publishInventory();
}

void MockEngine::writeFakeSystemVirtualDevices(const QVariantList &devices)
{
    const auto written = pw::virtualDevicesFromVariant(QVariant(devices), pw::VirtualDeviceScope::System);
    if (!written || !pw::validateVirtualDevices(*written, pw::VirtualDeviceScope::System)) {
        return;
    }
    QList<pw::VirtualDeviceState> kept;
    for (const pw::VirtualDeviceState &state : std::as_const(d->virtualDevices)) {
        if (state.device.scope == pw::VirtualDeviceScope::User) {
            kept.append(state);
        }
    }
    for (const pw::VirtualDevice &device : *written) {
        const auto before = std::ranges::find_if(d->virtualDevices, [&device](const pw::VirtualDeviceState &state) {
            return state.device.id == device.id && state.device.scope == pw::VirtualDeviceScope::System;
        });
        pw::VirtualDeviceState state;
        state.device = device;
        if (!device.enabled) {
            state.status = pw::VirtualDeviceStatus::Disabled;
        } else if (before != d->virtualDevices.end() && before->status != pw::VirtualDeviceStatus::Disabled) {
            state.status = before->status;
        } else {
            state.status = pw::VirtualDeviceStatus::Present;
        }
        kept.append(state);
    }
    d->virtualDevices = kept;
}

void MockEngine::refuseFakeVirtualDeviceNode(const QString &deviceId)
{
    pw::VirtualDeviceState *state = d->findVirtualDevice(deviceId);
    if (state == nullptr) {
        return;
    }
    state->status = pw::VirtualDeviceStatus::Missing;
    d->fail(ErrorCode::VirtualDeviceNodeRefused, {state->device.name});
    d->publishVirtualDevices();
    d->publishInventory();
}

void MockEngine::clearFakeVirtualDevices()
{
    d->virtualDevices.clear();
    d->unreadable.clear();
    d->refuseNext.reset();
    d->publishVirtualDevices();
    d->publishInventory();
}

void MockEngine::listVirtualDevices()
{
    d->publishVirtualDevices();
}

void MockEngine::createVirtualDevice(const QString &deviceJson)
{
    const std::expected<VirtualDeviceRequest, QString> request = virtualDeviceRequestFromJson(deviceJson);
    if (!request) {
        d->fail(ErrorCode::VirtualDeviceNotCreated, {virtualDeviceRequestName(deviceJson), request.error()});
        return;
    }
    const auto refuse = [this, &request](pw::VirtualDeviceError error) {
        d->fail(ErrorCode::VirtualDeviceNotCreated, {request->name, virtualDeviceReason(error)});
    };
    if (const std::optional<pw::VirtualDeviceError> refusal = d->refusalFirst()) {
        refuse(*refusal);
        return;
    }
    const std::expected<QString, pw::VirtualDeviceError> name = pw::validVirtualDeviceName(request->name);
    if (!name) {
        refuse(name.error());
        return;
    }
    const qsizetype own = std::ranges::count_if(d->virtualDevices, [](const pw::VirtualDeviceState &state) {
        return state.device.scope == pw::VirtualDeviceScope::User;
    });
    if (own >= pw::kMaxVirtualDevices) {
        refuse(pw::VirtualDeviceError::TooManyDevices);
        return;
    }

    pw::VirtualDeviceState state;
    state.device.id = pw::newVirtualDeviceId(d->allVirtualDevices());
    state.device.name = *name;
    state.device.direction = request->direction;
    state.device.channels = request->channels;
    // **Present at once**: there is no graph to wait for. A test that wants
    // the moment in between says so with setFakeVirtualDeviceStatus().
    state.status = pw::VirtualDeviceStatus::Present;
    const auto firstSystem = std::ranges::find_if(d->virtualDevices, [](const pw::VirtualDeviceState &listed) {
        return listed.device.scope == pw::VirtualDeviceScope::System;
    });
    d->virtualDevices.insert(firstSystem, state);

    Q_EMIT virtualDeviceCreated(state.device.id);
    d->publishVirtualDevices();
    d->publishInventory();
}

void MockEngine::renameVirtualDevice(const QString &deviceId, const QString &name)
{
    d->changeVirtualDevice(deviceId, [&name](pw::VirtualDeviceState &state) -> std::optional<pw::VirtualDeviceError> {
        const std::expected<QString, pw::VirtualDeviceError> valid = pw::validVirtualDeviceName(name);
        if (!valid) {
            return valid.error();
        }
        state.device.name = *valid;
        return std::nullopt;
    });
}

void MockEngine::setVirtualDeviceEnabled(const QString &deviceId, bool enabled)
{
    d->changeVirtualDevice(deviceId, [enabled](pw::VirtualDeviceState &state) -> std::optional<pw::VirtualDeviceError> {
        state.device.enabled = enabled;
        state.status = enabled ? pw::VirtualDeviceStatus::Present : pw::VirtualDeviceStatus::Disabled;
        return std::nullopt;
    });
}

void MockEngine::deleteVirtualDevice(const QString &deviceId)
{
    d->changeVirtualDevice(deviceId, [this](pw::VirtualDeviceState &state) -> std::optional<pw::VirtualDeviceError> {
        const QString id = state.device.id;
        d->virtualDevices.removeIf([&id](const pw::VirtualDeviceState &listed) { return listed.device.id == id; });
        return std::nullopt;
    });
}

void MockEngine::reloadVirtualDevices()
{
    d->publishVirtualDevices();
}

QString MockEngine::fakeRecordingFolder() const
{
    return d->recordingsHome.filePath(u"recordings"_s);
}

QString MockEngine::addFakeRecording(const QString &sessionId, const QString &name, double seconds, bool inProgress)
{
    const QString path = QDir(fakeRecordingFolder()).filePath(name);
    // **A real file**: a tone that swells and dies away, so a waveform drawn
    // from it has a shape a test can see, and something to trash.
    record::FileSpec spec;
    spec.dither = false;
    std::expected<record::AudioFileWriter, record::FileError> writer =
        record::AudioFileWriter::create(std::filesystem::path{path.toStdString()}, spec);
    if (!writer) {
        return {};
    }
    const auto frames = static_cast<std::size_t>(std::max(0.0, seconds) * kFakeSampleRate);
    std::vector<float> samples(frames * 2, 0.0f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double along = static_cast<double>(frame) / static_cast<double>(std::max<std::size_t>(frames, 1));
        const auto value = static_cast<float>(0.8 * std::sin(std::numbers::pi * along)
                                              * std::sin(2.0 * std::numbers::pi * 440.0 * frame / kFakeSampleRate));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = value;
    }
    if (!writer->write(samples) || !writer->close()) {
        return {};
    }
    d->library.fileStarted(sessionId, u"recorder"_s, path);
    if (!inProgress) {
        finishFakeRecording(sessionId, path);
    }
    return path;
}

QString MockEngine::writeFakeImpulse(const QString &name, int channels, double seconds, int rate)
{
    const QString path = QDir(fakeRecordingFolder()).filePath(name);
    record::FileSpec spec;
    spec.sampleFormat = record::SampleFormat::Float32;
    spec.sampleRate = static_cast<std::uint32_t>(std::max(rate, 1));
    spec.channels = static_cast<std::uint32_t>(std::max(channels, 1));
    spec.dither = false;
    std::expected<record::AudioFileWriter, record::FileError> writer =
        record::AudioFileWriter::create(std::filesystem::path{path.toStdString()}, spec);
    if (!writer) {
        return {};
    }
    const auto frames = static_cast<std::size_t>(std::max(0.0, seconds) * spec.sampleRate);
    std::vector<float> samples(frames * spec.channels, 0.0f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(std::pow(10.0, -3.0 * static_cast<double>(frame) / frames));
        for (std::size_t channel = 0; channel < spec.channels; ++channel) {
            samples[frame * spec.channels + channel] = frame % 7 == 0 ? value : 0.0f;
        }
    }
    if (!writer->write(samples) || !writer->close()) {
        return {};
    }
    return path;
}

void MockEngine::finishFakeRecording(const QString &sessionId, const QString &path)
{
    d->library.fileFinished(sessionId, u"recorder"_s, path);
}

void MockEngine::waitForFakeRecordings()
{
    d->library.waitForIdle();
}

void MockEngine::listPlugins()
{
    Q_EMIT pluginsChanged(asJson(pluginsJson(d->fakePlugins, d->fakeBlocked, false, d->fakeIncomplete)));
}

void MockEngine::describePlugin(const QString &format, const QString &id)
{
    QJsonArray params;
    for (const InstalledPlugin &fake : d->fakePlugins) {
        if (fake.format == format && fake.id == id) {
            params = fake.params;
        }
    }
    Q_EMIT pluginDescribed(format, id, asJson(QJsonObject{{"params"_L1, params}}));
}

void MockEngine::rescanPlugins(bool retryBlocked)
{
    ++d->rescans;
    d->lastRescanRetried = retryBlocked;
    // **As the engine says it**: that a scan has started, and then what it
    // found. A retried candidate that still fails stays where it was.
    Q_EMIT pluginsChanged(asJson(pluginsJson(d->fakePlugins, d->fakeBlocked, true, d->fakeIncomplete)));
    QMetaObject::invokeMethod(
        this,
        [this] {
            Q_EMIT pluginsChanged(asJson(pluginsJson(d->fakePlugins, d->fakeBlocked, false, d->fakeIncomplete)));
        },
        Qt::QueuedConnection);
}

void MockEngine::addFakePlugin(const QString &format,
                               const QString &id,
                               const QString &name,
                               const QString &vendor,
                               int audioInputs,
                               int audioOutputs,
                               const QString &paramsJson)
{
    const QJsonArray params = QJsonDocument::fromJson(paramsJson.toUtf8()).array();
    d->fakePlugins.append(InstalledPlugin{
        format, id, name, vendor, {}, audioInputs, audioOutputs, static_cast<int>(params.size()), {}, params});
    listPlugins();
}

void MockEngine::addFakeBlockedPlugin(const QString &format, const QString &path, const QString &reason)
{
    d->fakeBlocked.append(BlockedCandidate{format, path, reason});
    listPlugins();
}

void MockEngine::addFakeIncompletePlugins(const QString &format, const QString &path)
{
    d->fakeIncomplete.append(BlockedCandidate{format, path, u"incomplete"_s});
    listPlugins();
}

void MockEngine::clearFakePlugins()
{
    d->resetFakePlugins();
    listPlugins();
}

int MockEngine::fakeRescans() const
{
    return d->rescans;
}

bool MockEngine::fakeLastRescanRetried() const
{
    return d->lastRescanRetried;
}

void MockEngine::listRecordings(const QString &sessionId)
{
    Q_EMIT recordingsChanged(sessionId, asJson(d->library.recordings(sessionId)));
}

void MockEngine::rescanRecordings()
{
    d->library.rescan();
}

void MockEngine::previewRecording(const QString &path)
{
    if (path == d->preview.path && d->preview.state == "playing"_L1) {
        d->announcePreview();
        return;
    }
    if (path != d->preview.path || d->preview.heldSeconds >= d->preview.duration) {
        const std::optional<record::AudioFileReader> reader =
            record::AudioFileReader::open(std::filesystem::path{path.toStdString()});
        if (!reader) {
            d->preview.path = path;
            d->preview.duration = 0.0;
            d->preview.heldSeconds = 0.0;
            d->preview.state = u"stopped"_s;
            d->preview.tick.stop();
            d->announcePreview();
            d->fail(ErrorCode::RecordingPreviewFailed, {path, u"unreadable"_s});
            return;
        }
        d->preview.path = path;
        d->preview.duration = static_cast<double>(reader->frames()) / reader->sampleRate();
        d->preview.heldSeconds = 0.0;
    }
    if (!d->audioSystemRunning) {
        d->fail(ErrorCode::RecordingPreviewFailed, {path, u"noAudioSystem"_s});
        return;
    }
    d->setPreviewState(u"playing"_s);
}

void MockEngine::pausePreview()
{
    if (d->preview.state == "playing"_L1) {
        d->setPreviewState(u"paused"_s);
    }
}

void MockEngine::seekPreview(double seconds)
{
    if (d->preview.path.isEmpty()) {
        return;
    }
    const QString state = d->preview.state == "playing"_L1 ? u"playing"_s : u"paused"_s;
    d->preview.heldSeconds = std::clamp(seconds, 0.0, d->preview.duration);
    d->preview.since.restart();
    d->preview.state = state;
    d->setPreviewState(state);
}

void MockEngine::stopPreview()
{
    d->preview.heldSeconds = 0.0;
    d->preview.state = u"stopped"_s;
    d->preview.tick.stop();
    d->announcePreview();
}

void MockEngine::getRecording(const QString &path)
{
    if (const std::optional<QJsonObject> details = d->library.details(path)) {
        Q_EMIT recordingDetails(path, asJson(*details));
        return;
    }
    d->fail(ErrorCode::RecordingNotFound, {path});
}

void MockEngine::renameRecording(const QString &path, const QString &name)
{
    // A file being listened to is let go of first: the preview names it.
    if (d->preview.path == path) {
        stopPreview();
    }
    const std::expected<QString, QString> renamed = d->library.rename(path, name);
    if (!renamed) {
        d->fail(ErrorCode::RecordingRenameRefused, {path, renamed.error()});
        return;
    }
    Q_EMIT recordingRenamed(path, *renamed);
}

void MockEngine::setRecordingTags(const QString &path, const QString &tagsJson)
{
    if (d->preview.path == path) {
        stopPreview();
    }
    const QJsonObject tags = QJsonDocument::fromJson(tagsJson.toUtf8()).object();
    if (!d->library.saveTags(path, tags)) {
        d->fail(ErrorCode::RecordingTagsNotSaved, {path});
        return;
    }
    getRecording(path);
}

} // namespace rb::core
