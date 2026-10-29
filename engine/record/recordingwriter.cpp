// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/recordingwriter.h"

#include "record/filename.h"
#include "record/repair.h"
#include "record/resampler.h"
#include "rt/audiotap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace rb::record
{

namespace
{

/// How many frames go to the file at once.
constexpr std::size_t kChunkFrames = 4096;

/// Whether Opus is made at this rate. It is made at 48 kHz and the rates
/// that divide it, and at nothing else.
[[nodiscard]] bool opusTakes(std::uint32_t rate)
{
    return rate == 8000 || rate == 12000 || rate == 16000 || rate == 24000 || rate == 48000;
}

/// The rate a file is made at, for a session at \a sessionRate.
[[nodiscard]] std::uint32_t fileRateFor(const RecordingSettings &settings, std::uint32_t sessionRate)
{
    if (settings.fileRate != 0) {
        return settings.fileRate;
    }
    if (settings.format == FileFormat::Opus && !opusTakes(sessionRate)) {
        return 48000;
    }
    return sessionRate;
}

[[nodiscard]] std::chrono::local_seconds localNow()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::chrono::zoned_time{std::chrono::current_zone(), now}.get_local_time();
}

/// \a seconds as frames at \a rate, rounded, and at least one.
[[nodiscard]] std::uint64_t framesOf(double seconds, std::uint32_t rate)
{
    return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(seconds * rate)));
}

} // namespace

FileLimits safetyLimits(FileFormat format) noexcept
{
    FileLimits limits;
    if (format == FileFormat::Aiff) {
        limits.splitEveryBytes = std::uint64_t{2} << 30;
        limits.splitEverySeconds = 24.0 * 3600.0;
    }
    return limits;
}

/// One recording, as the writer thread keeps it.
struct RecordingWriter::Recording {
    std::uint64_t key = 0;
    std::shared_ptr<rt::AudioTap> tap;
    /// Whether what arrives is to be written, or let go of.
    bool recording = false;
    std::optional<RecordingSettings> settings;
    std::optional<AudioFileWriter> file;
    std::optional<std::filesystem::path> journalEntry;
    /// The rate the tap last said its frames are at; zero until it has.
    std::uint32_t sampleRate = 0;
    /// A marker taken off the tap whose frame has not been reached yet.
    std::optional<rt::TapMarker> pending;
    std::uint64_t droppedSeen = 0;
    std::chrono::steady_clock::time_point lastRefresh;
    /// The open file's rate, and what converts the tap's rate to it when
    /// the two differ.
    std::uint32_t fileRate = 0;
    std::uint32_t fileChannels = 2;
    std::unique_ptr<Resampler> resampler;
    /// Whether the open file has said the disk is getting full, and when
    /// the room was last looked at.
    bool warnedLowSpace = false;
    std::chrono::steady_clock::time_point lastSpaceCheck;

    /// Paused: the file stays open, and what arrives is let go of.
    bool paused = false;
    /// The counter the next file is named with, from 1 when the recording
    /// starts and one on from the last file's at every split.
    unsigned counter = 1;
    /// Frames, as the tap gave them, in this file and in the recording.
    std::uint64_t fileFrames = 0;
    std::uint64_t totalFrames = 0;
    /// How many silent frames in a row have gone by, and whether a split on
    /// silence is waiting for sound to start the next file.
    std::uint64_t silentFrames = 0;
    bool waitingForSound = false;
    /// What the open file was named with: its time and its counter, which
    /// its tags are filled in with too.
    FileNameValues fileValues;
};

RecordingWriter::RecordingWriter(EventSink sink, WriterOptions options)
    : m_sink(std::move(sink))
    , m_options(std::move(options))
{
    if (!m_options.clock) {
        m_options.clock = &localNow;
    }
    m_thread = std::thread([this] { run(); });
}

RecordingWriter::~RecordingWriter()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_one();
    m_thread.join();
}

void RecordingWriter::attach(std::uint64_t key, std::shared_ptr<rt::AudioTap> tap)
{
    {
        const std::lock_guard lock(m_mutex);
        m_taps[key] = tap;
    }
    post(Attach{key, std::move(tap)});
}

void RecordingWriter::start(std::uint64_t key, RecordingSettings settings)
{
    const std::uint64_t from = positionOf(key);
    post(Start{key, std::move(settings), from});
}

void RecordingWriter::pause(std::uint64_t key)
{
    post(Control{key, Control::Action::Pause, positionOf(key)});
}

void RecordingWriter::resume(std::uint64_t key)
{
    post(Control{key, Control::Action::Resume, positionOf(key)});
}

void RecordingWriter::split(std::uint64_t key)
{
    post(Control{key, Control::Action::Split, positionOf(key)});
}

void RecordingWriter::finish(std::uint64_t key)
{
    const std::uint64_t upTo = positionOf(key);
    post(Finish{key, upTo});
}

void RecordingWriter::detach(std::uint64_t key)
{
    {
        const std::lock_guard lock(m_mutex);
        m_taps.erase(key);
    }
    post(Detach{key});
}

std::optional<std::uint64_t> RecordingWriter::fenceFor(std::uint64_t key)
{
    std::optional<std::uint64_t> fence;
    const auto consider = [&fence, key](const Command &command) {
        std::optional<std::uint64_t> at;
        if (const auto *start = std::get_if<Start>(&command); start != nullptr && start->key == key) {
            at = start->from;
        } else if (const auto *finish = std::get_if<Finish>(&command); finish != nullptr && finish->key == key) {
            at = finish->upTo;
        } else if (const auto *control = std::get_if<Control>(&command); control != nullptr && control->key == key) {
            at = control->at;
        }
        if (at && (!fence || *at < *fence)) {
            fence = at;
        }
    };
    for (std::size_t index = m_batchNext; index < m_batch.size(); ++index) {
        consider(m_batch[index]);
    }
    const std::lock_guard lock(m_mutex);
    for (const Command &command : m_queue) {
        consider(command);
    }
    return fence;
}

std::uint64_t RecordingWriter::positionOf(std::uint64_t key)
{
    const std::lock_guard lock(m_mutex);
    const auto found = m_taps.find(key);
    return found == m_taps.end() ? 0 : found->second->framesWritten();
}

void RecordingWriter::sync()
{
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    post(Sync{std::move(done)});
    finished.wait();
}

void RecordingWriter::post(Command command)
{
    {
        const std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(command));
    }
    m_wake.notify_one();
}

void RecordingWriter::run()
{
    for (;;) {
        bool stopping = false;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait_for(lock, m_options.pollInterval, [this] { return !m_queue.empty() || m_stopping; });
            m_batch.clear();
            m_batch.swap(m_queue);
            m_batchNext = 0;
            stopping = m_stopping;
        }
        while (m_batchNext < m_batch.size()) {
            Command command = std::move(m_batch[m_batchNext]);
            ++m_batchNext;
            apply(command);
        }
        m_batch.clear();
        m_batchNext = 0;
        drainAll();
        if (stopping) {
            for (auto &[key, recording] : m_recordings) {
                drain(*recording);
                closeFile(*recording);
            }
            m_recordings.clear();
            return;
        }
    }
}

void RecordingWriter::apply(Command &command)
{
    if (auto *attach = std::get_if<Attach>(&command)) {
        auto recording = std::make_unique<Recording>();
        recording->key = attach->key;
        recording->tap = std::move(attach->tap);
        recording->droppedSeen = recording->tap->droppedFrames();
        m_recordings[attach->key] = std::move(recording);
        return;
    }
    if (auto *start = std::get_if<Start>(&command)) {
        const auto found = m_recordings.find(start->key);
        if (found == m_recordings.end()) {
            return;
        }
        Recording &recording = *found->second;
        if (!recording.recording) {
            // **From where start() was called.** What came before arrived
            // while nobody was recording, and a file that began with it
            // would begin before the person pressed anything; what came
            // after is the recording, however long this took to get to.
            drain(recording, start->from);
            recording.droppedSeen = recording.tap->droppedFrames();
            recording.recording = true;
            recording.paused = false;
            recording.counter = 1;
            recording.totalFrames = 0;
            recording.silentFrames = 0;
            recording.waitingForSound = false;
        }
        recording.settings = std::move(start->settings);
        return;
    }
    if (auto *finish = std::get_if<Finish>(&command)) {
        const auto found = m_recordings.find(finish->key);
        if (found != m_recordings.end()) {
            drain(*found->second, finish->upTo);
            closeFile(*found->second);
            found->second->recording = false;
        }
        return;
    }
    if (auto *detach = std::get_if<Detach>(&command)) {
        const auto found = m_recordings.find(detach->key);
        if (found != m_recordings.end()) {
            drain(*found->second);
            closeFile(*found->second);
            m_recordings.erase(found);
        }
        return;
    }
    if (auto *sync = std::get_if<Sync>(&command)) {
        drainAll();
        sync->done->set_value();
        return;
    }
    if (auto *control = std::get_if<Control>(&command)) {
        const auto found = m_recordings.find(control->key);
        if (found == m_recordings.end()) {
            return;
        }
        Recording &recording = *found->second;
        // Everything before the frame it was asked at goes where it would
        // have gone anyway; the command applies from that frame on.
        drain(recording, control->at);
        RecordingEvent said;
        said.key = recording.key;
        if (recording.file) {
            said.path = recording.file->path();
        }
        switch (control->action) {
        case Control::Action::Pause:
            if (recording.recording && !recording.paused) {
                recording.paused = true;
                said.kind = RecordingEvent::Kind::Paused;
                emit(std::move(said));
            }
            break;
        case Control::Action::Resume:
            if (recording.recording && recording.paused) {
                recording.paused = false;
                said.kind = RecordingEvent::Kind::Resumed;
                emit(std::move(said));
            }
            break;
        case Control::Action::Split:
            if (recording.file) {
                closeFile(recording);
                ++recording.counter;
            }
            break;
        }
    }
}

void RecordingWriter::drainAll()
{
    for (auto &[key, recording] : m_recordings) {
        drain(*recording);
    }
}

void RecordingWriter::drain(Recording &recording, std::optional<std::uint64_t> upTo)
{
    rt::AudioTap &tap = *recording.tap;
    const std::size_t channels = tap.channelCount();
    m_buffer.resize(kChunkFrames * channels);
    // **No further than what was there on the way in**, so one busy
    // recording cannot keep the others waiting for ever.
    const std::uint64_t written = tap.framesWritten();
    std::uint64_t target = upTo ? std::min(*upTo, written) : written;
    if (!upTo) {
        if (const std::optional<std::uint64_t> fence = fenceFor(recording.key)) {
            target = std::min(target, *fence);
        }
    }
    if ((!recording.recording || recording.paused) && !upTo) {
        // **Nobody is recording, and the newest half of the tap is kept**:
        // a start() that has been asked for and not yet carried out records
        // from where it was asked, and what it will record has to be here
        // when it is. The older half goes, so the audio thread always has
        // room.
        const std::uint64_t keep = tap.capacityFrames() / 2;
        target = written > keep ? std::min(target, written - keep) : 0;
    }

    for (;;) {
        // The markers that are due: a rate that starts at or before the next
        // frame to be read applies to it.
        for (;;) {
            if (!recording.pending) {
                rt::TapMarker marker;
                if (!tap.popMarker(marker)) {
                    break;
                }
                recording.pending = marker;
            }
            if (recording.pending->atFrame > tap.framesRead()) {
                break;
            }
            if (recording.pending->kind == rt::TapMarker::Kind::Rate
                && recording.pending->value != recording.sampleRate) {
                if (recording.file && recording.settings && recording.settings->fileRate != 0) {
                    // **A file at a rate of its own keeps it**: what converts
                    // into it changes, and the file goes on.
                    if (recording.resampler) {
                        const std::span<const float> tail = recording.resampler->flush();
                        recording.resampler.reset();
                        (void)writeToFile(recording, tail);
                    }
                } else {
                    // A file at the session's rate has one rate. The next
                    // frames open another.
                    closeFile(recording);
                }
                recording.sampleRate = recording.pending->value;
            }
            recording.pending.reset();
        }

        const std::uint64_t read = tap.framesRead();
        if (read >= target) {
            break;
        }
        std::uint64_t upTo = target;
        if (recording.pending) {
            upTo = std::min(upTo, recording.pending->atFrame);
        }
        const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(upTo - read, kChunkFrames));
        if (frames == 0) {
            break;
        }

        if (!recording.recording || recording.paused || recording.sampleRate == 0) {
            (void)tap.discard(frames);
            continue;
        }
        const std::size_t got = tap.read(m_buffer, frames);
        take(recording, std::span<const float>{m_buffer}.first(got * channels));
    }

    checkSpace(recording);

    const std::uint64_t dropped = tap.droppedFrames();
    if (dropped > recording.droppedSeen) {
        if (recording.recording) {
            RecordingEvent overflow;
            overflow.kind = RecordingEvent::Kind::Overflow;
            overflow.key = recording.key;
            overflow.frames = dropped - recording.droppedSeen;
            overflow.sampleRate = recording.sampleRate;
            if (recording.file) {
                overflow.path = recording.file->path();
            }
            emit(std::move(overflow));
        }
        recording.droppedSeen = dropped;
    }

    if (recording.file && std::chrono::steady_clock::now() - recording.lastRefresh >= m_options.headerInterval) {
        recording.file->refreshHeader();
        recording.lastRefresh = std::chrono::steady_clock::now();
    }
}

void RecordingWriter::take(Recording &recording, std::span<const float> samples)
{
    const std::size_t channels = recording.tap->channelCount();
    const FileLimits &limits = recording.settings->limits;
    if (limits.silence == FileLimits::Silence::Keep) {
        writeFrames(recording, samples);
        return;
    }
    const float threshold = std::pow(10.0f, limits.silenceThresholdDb / 20.0f);
    const std::uint64_t enough = framesOf(limits.silenceSeconds, recording.sampleRate);
    const std::size_t frames = samples.size() / channels;

    // Frame by frame: whether it is kept, and whether the file ends after
    // it. Kept frames go to the file in runs.
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
    std::size_t run = kNone;
    const auto flush = [&](std::size_t end) {
        if (run != kNone && end > run) {
            writeFrames(recording, samples.subspan(run * channels, (end - run) * channels));
        }
        run = kNone;
    };
    for (std::size_t frame = 0; frame < frames && recording.recording; ++frame) {
        bool silent = true;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            silent = silent && std::fabs(samples[frame * channels + channel]) < threshold;
        }
        bool keep = true;
        bool ends = false;
        if (!silent) {
            recording.silentFrames = 0;
            recording.waitingForSound = false;
        } else if (recording.waitingForSound) {
            keep = false;
        } else {
            ++recording.silentFrames;
            if (recording.silentFrames > enough) {
                keep = false; // only Remove gets this far
            } else if (recording.silentFrames == enough) {
                ends = limits.silence != FileLimits::Silence::Remove;
            }
        }
        if (keep) {
            if (run == kNone) {
                run = frame;
            }
        } else {
            flush(frame);
        }
        if (ends) {
            flush(frame + 1);
            if (limits.silence == FileLimits::Silence::Split) {
                // The file ends with the silence that ended it; the next
                // starts with the sound that comes after.
                if (recording.file) {
                    closeFile(recording);
                    ++recording.counter;
                }
                recording.waitingForSound = true;
            } else {
                stopByItself(recording, RecordingEvent::StopReason::Silence);
            }
        }
    }
    flush(frames);
}

void RecordingWriter::writeFrames(Recording &recording, std::span<const float> samples)
{
    const std::size_t channels = recording.tap->channelCount();
    const FileLimits &limits = recording.settings->limits;
    const FileLimits safety = safetyLimits(recording.settings->format);
    const auto smallest = [](auto a, auto b) { return a == 0 ? b : (b == 0 ? a : std::min(a, b)); };
    const std::uint64_t splitFrames =
        smallest(limits.splitEverySeconds > 0.0 ? framesOf(limits.splitEverySeconds, recording.sampleRate) : 0,
                 safety.splitEverySeconds > 0.0 ? framesOf(safety.splitEverySeconds, recording.sampleRate) : 0);
    const std::uint64_t splitBytes = smallest(limits.splitEveryBytes, safety.splitEveryBytes);
    const std::uint64_t endFrames =
        limits.endAfterSeconds > 0.0 ? framesOf(limits.endAfterSeconds, recording.sampleRate) : 0;

    while (!samples.empty() && recording.recording) {
        if (!recording.file && !openFile(recording)) {
            // Said once; what arrives after it is let go of until somebody
            // starts the recording again.
            recording.recording = false;
            return;
        }
        // **No further than the next limit**, so a split or an end falls on
        // the exact frame.
        std::uint64_t frames = samples.size() / channels;
        if (splitFrames != 0) {
            frames = std::min(frames, splitFrames - recording.fileFrames);
        }
        if (endFrames != 0) {
            frames = std::min(frames, endFrames - recording.totalFrames);
        }
        std::span<const float> part = samples.first(frames * channels);
        samples = samples.subspan(frames * channels);
        if (recording.fileChannels == 1 && channels > 1) {
            // **Mono is the sides mixed**, half of each, so a sound on one side
            // alone is heard at half its level rather than lost.
            m_mono.resize(frames);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                float sum = 0.0f;
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    sum += part[frame * channels + channel];
                }
                m_mono[frame] = sum / static_cast<float>(channels);
            }
            part = m_mono;
        }
        if (recording.fileRate != recording.sampleRate) {
            if (!recording.resampler) {
                recording.resampler =
                    Resampler::create(recording.sampleRate, recording.fileRate, recording.fileChannels);
            }
            if (!recording.resampler) {
                RecordingEvent failed;
                failed.kind = RecordingEvent::Kind::Failed;
                failed.key = recording.key;
                failed.path = recording.file->path();
                failed.error = FileError::UnsupportedFormat;
                emit(std::move(failed));
                closeFile(recording);
                recording.recording = false;
                return;
            }
            part = recording.resampler->process(part);
        }
        if (!writeToFile(recording, part)) {
            return;
        }
        recording.fileFrames += frames;
        recording.totalFrames += frames;

        if (endFrames != 0 && recording.totalFrames >= endFrames) {
            stopByItself(recording, RecordingEvent::StopReason::TimeLimit);
            return;
        }
        std::error_code error;
        const bool bigEnough =
            splitBytes != 0 && std::filesystem::file_size(recording.file->path(), error) >= splitBytes;
        if ((splitFrames != 0 && recording.fileFrames >= splitFrames) || bigEnough) {
            closeFile(recording);
            ++recording.counter;
        }
    }
}

void RecordingWriter::stopByItself(Recording &recording, RecordingEvent::StopReason reason)
{
    RecordingEvent stopped;
    stopped.kind = RecordingEvent::Kind::Stopped;
    stopped.key = recording.key;
    stopped.stopReason = reason;
    if (recording.file) {
        stopped.path = recording.file->path();
    }
    closeFile(recording);
    recording.recording = false;
    recording.paused = false;
    emit(std::move(stopped));
}

bool RecordingWriter::writeToFile(Recording &recording, std::span<const float> samples)
{
    if (!recording.file) {
        return false;
    }
    const std::expected<void, FileError> written = recording.file->write(samples);
    if (written) {
        return true;
    }
    RecordingEvent failed;
    failed.kind = RecordingEvent::Kind::Failed;
    failed.key = recording.key;
    failed.path = recording.file->path();
    failed.error = written.error();
    emit(std::move(failed));
    // What it could not write is not kept to be tried again.
    recording.resampler.reset();
    closeFile(recording);
    recording.recording = false;
    return false;
}

std::optional<std::uint64_t> RecordingWriter::freeSpaceFor(const std::filesystem::path &folder) const
{
    if (m_options.freeSpace) {
        return m_options.freeSpace(folder);
    }
    // The folder may not be made yet: the disk it will be on is the one its
    // nearest existing parent is on.
    std::error_code error;
    std::filesystem::path existing = folder;
    while (!existing.empty() && !std::filesystem::exists(existing, error)) {
        const std::filesystem::path parent = existing.parent_path();
        if (parent == existing) {
            break;
        }
        existing = parent;
    }
    const std::filesystem::space_info space = std::filesystem::space(existing.empty() ? "." : existing, error);
    if (error) {
        return std::nullopt;
    }
    return space.available;
}

void RecordingWriter::checkSpace(Recording &recording)
{
    if (!recording.file) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - recording.lastSpaceCheck < m_options.spaceInterval) {
        return;
    }
    recording.lastSpaceCheck = now;
    const std::optional<std::uint64_t> free = freeSpaceFor(recording.file->path().parent_path());
    if (!free) {
        return;
    }
    if (*free < m_options.reserveBytes) {
        // **Finished while it still can be**, and said why.
        RecordingEvent full;
        full.kind = RecordingEvent::Kind::Failed;
        full.key = recording.key;
        full.path = recording.file->path();
        full.error = FileError::DiskFull;
        full.bytesFree = *free;
        emit(std::move(full));
        closeFile(recording);
        recording.recording = false;
        return;
    }
    if (*free < m_options.lowSpaceBytes && !recording.warnedLowSpace) {
        recording.warnedLowSpace = true;
        RecordingEvent low;
        low.kind = RecordingEvent::Kind::LowDiskSpace;
        low.key = recording.key;
        low.path = recording.file->path();
        low.bytesFree = *free;
        emit(std::move(low));
    }
}

bool RecordingWriter::openFile(Recording &recording)
{
    if (!recording.settings) {
        return false;
    }
    const RecordingSettings &settings = *recording.settings;
    // **No file on a disk that is as good as full**: it would be stopped
    // before it had anything in it.
    const std::optional<std::uint64_t> free = freeSpaceFor(settings.folder);
    if (free && *free < m_options.reserveBytes) {
        RecordingEvent full;
        full.kind = RecordingEvent::Kind::Failed;
        full.key = recording.key;
        full.path = settings.folder;
        full.error = FileError::DiskFull;
        full.bytesFree = *free;
        emit(std::move(full));
        return false;
    }
    FileNameValues values;
    values.session = settings.sessionName;
    values.block = settings.blockName;
    values.when = m_options.clock();
    values.counter = recording.counter;

    FileSpec spec;
    spec.format = settings.format;
    spec.sampleFormat = settings.sampleFormat;
    spec.sampleRate = fileRateFor(settings, recording.sampleRate);
    spec.channels = settings.mono ? 1 : recording.tap->channelCount();
    spec.bitrateKbps = settings.bitrateKbps;
    spec.variableBitrate = settings.variableBitrate;
    spec.ditherSeed = m_options.ditherSeed;
    spec.dither = m_options.dither;

    const auto exists = [](const std::filesystem::path &path) {
        std::error_code error;
        return std::filesystem::exists(path, error);
    };
    std::expected<AudioFileWriter, FileError> made = std::unexpected(FileError::FileNotCreated);
    std::filesystem::path path;
    // Twice, for the file somebody else made between the look and the
    // making: O_EXCL refuses it, and the second look finds the next name.
    for (int attempt = 0; attempt < 2; ++attempt) {
        path = freePath(settings.folder,
                        settings.fileNamePattern,
                        values,
                        fileExtension(settings.format),
                        exists,
                        &recording.counter);
        made = AudioFileWriter::create(path, spec);
        if (made || made.error() != FileError::FileExists) {
            break;
        }
    }
    if (!made) {
        RecordingEvent failed;
        failed.kind = RecordingEvent::Kind::Failed;
        failed.key = recording.key;
        failed.path = path;
        failed.error = made.error();
        emit(std::move(failed));
        return false;
    }

    // **Written down before the first frame**, so there is no moment at
    // which a crash leaves a file the next start does not know about.
    if (!m_options.journalFolder.empty()) {
        recording.journalEntry = RecordingJournal(m_options.journalFolder).add(path);
    }
    recording.file.emplace(std::move(*made));
    recording.fileValues = values;
    recording.fileValues.counter = recording.counter;
    recording.fileFrames = 0;
    recording.fileRate = spec.sampleRate;
    recording.fileChannels = spec.channels;
    recording.resampler.reset();
    recording.lastRefresh = std::chrono::steady_clock::now();
    recording.lastSpaceCheck = recording.lastRefresh;
    recording.warnedLowSpace = false;

    RecordingEvent started;
    started.kind = RecordingEvent::Kind::FileStarted;
    started.key = recording.key;
    started.path = path;
    started.sampleRate = spec.sampleRate;
    emit(std::move(started));
    if (free && *free < m_options.lowSpaceBytes) {
        recording.warnedLowSpace = true;
        RecordingEvent low;
        low.kind = RecordingEvent::Kind::LowDiskSpace;
        low.key = recording.key;
        low.path = path;
        low.bytesFree = *free;
        emit(std::move(low));
    }
    return true;
}

void RecordingWriter::closeFile(Recording &recording)
{
    if (!recording.file) {
        return;
    }
    // What the converter was holding back belongs at the end of this file.
    if (recording.resampler) {
        const std::span<const float> tail = recording.resampler->flush();
        std::unique_ptr<Resampler> done = std::move(recording.resampler);
        if (!tail.empty() && !recording.file->write(tail)) {
            RecordingEvent failed;
            failed.kind = RecordingEvent::Kind::Failed;
            failed.key = recording.key;
            failed.path = recording.file->path();
            failed.error = FileError::WriteFailed;
            emit(std::move(failed));
        }
    }
    RecordingEvent finished;
    finished.kind = RecordingEvent::Kind::FileFinished;
    finished.key = recording.key;
    finished.path = recording.file->path();
    finished.frames = recording.file->framesWritten();
    finished.sampleRate = recording.file->spec().sampleRate;
    const std::expected<void, FileError> closed = recording.file->close();
    const FileFormat format = recording.file->spec().format;
    recording.file.reset();
    // **Tags go on a finished file**, when its length is final and before
    // anybody is told it is there.
    if (closed && recording.settings && !recording.settings->tags.isEmpty()
        && !writeTags(finished.path, format, renderTags(recording.settings->tags, recording.fileValues))) {
        RecordingEvent untagged;
        untagged.kind = RecordingEvent::Kind::TagsNotWritten;
        untagged.key = recording.key;
        untagged.path = finished.path;
        emit(std::move(untagged));
    }
    if (recording.journalEntry) {
        RecordingJournal(m_options.journalFolder).remove(*recording.journalEntry);
        recording.journalEntry.reset();
    }
    if (!closed) {
        RecordingEvent failed;
        failed.kind = RecordingEvent::Kind::Failed;
        failed.key = recording.key;
        failed.path = finished.path;
        failed.error = closed.error();
        emit(std::move(failed));
    }
    emit(std::move(finished));
}

void RecordingWriter::emit(RecordingEvent event)
{
    if (m_sink) {
        m_sink(event);
    }
}

} // namespace rb::record
