// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "record/audiofile.h"
#include "record/filename.h"
#include "record/tags.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace rb::rt
{
class AudioTap;
} // namespace rb::rt

namespace rb::record
{

/// **When a recording splits itself into files, or stops by itself**.
/// Everything here is counted in the frames the session hands over, so a
/// split falls on an exact sample whatever the file's own rate is.
struct FileLimits {
    /// A new file every so many seconds of recording; zero never.
    double splitEverySeconds = 0.0;
    /// A new file once a file has grown to so many bytes; zero never. Looked
    /// at after each piece written, so a file ends within a few kilobytes of
    /// it.
    std::uint64_t splitEveryBytes = 0;
    /// The recording ends, by itself, after so many seconds of it; zero
    /// never. Counted across its files, and not while it is paused.
    double endAfterSeconds = 0.0;

    /// What a stretch of silence does.
    enum class Silence : std::uint8_t {
        Keep, ///< Nothing: it is recorded like anything else.
        Remove, ///< After silenceSeconds of it, the rest is left out until sound returns.
        Split, ///< After silenceSeconds of it, the file ends; a new one starts when sound returns.
        Stop, ///< After silenceSeconds of it, the recording ends.
    };
    Silence silence = Silence::Keep;
    double silenceSeconds = 2.0;
    /// Quieter than this on every channel is silence. −96 dB is digital
    /// silence and nothing else; −40 dB takes in the hiss of a quiet room.
    float silenceThresholdDb = -60.0f;
};

/// The safety split for a format that needs one, or nothing: an AIFF cannot
/// say it is longer than 4 GB, so it starts a new file at 2 GB, or after 24
/// hours, whichever comes first. The others have no ceiling that matters —
/// a WAV becomes RF64, and FLAC, MP3 and Ogg are streams.
[[nodiscard]] FileLimits safetyLimits(FileFormat format) noexcept;

/// What a file is made from: read off the block when recording starts, so
/// a setting changed in the middle of a recording takes effect at the next
/// file rather than half-way through this one.
struct RecordingSettings {
    FileFormat format = FileFormat::Wav;
    SampleFormat sampleFormat = SampleFormat::Int24;
    std::filesystem::path folder;
    std::string fileNamePattern;
    std::string sessionName;
    std::string blockName;
    /// The file's rate, or zero for the session's. A file at another
    /// rate is converted on the writer's thread, and keeps its rate when the
    /// session's changes under it; one at the session's follows it, a new
    /// file for each rate. An Opus file in a session at a rate Opus does not
    /// take (44.1 kHz, say) is made at 48 kHz.
    std::uint32_t fileRate = 0;
    /// For a lossy format: the rate aimed at and whether it may vary.
    std::uint32_t bitrateKbps = 256;
    bool variableBitrate = true;
    /// One channel, the two sides mixed, rather than two.
    bool mono = false;
    /// When the recording splits itself into files, or stops by itself.
    FileLimits limits;
    /// The tags each file gets when it is finished, tokens and all:
    /// filled in for each file with its own time and counter.
    Tags tags;
};

/// Something the writer has to report. Every file that is started is
/// finished, whatever else happens to it.
struct RecordingEvent {
    enum class Kind : std::uint8_t {
        FileStarted, ///< A file was made and audio is going into it.
        FileFinished, ///< A file was closed; \a frames is how much it holds.
        Failed, ///< A file could not be made or written; \a error says why.
        /// Audio was lost because the disk did not keep up: \a frames of it,
        /// between two looks.
        Overflow,
        /// The disk the file is on has less than WriterOptions::lowSpaceBytes
        /// left: \a bytesFree. Once a file; the recording carries on.
        LowDiskSpace,
        /// pause() or resume() has been carried out.
        Paused,
        Resumed,
        /// The recording ended by itself, for \a stopReason; its file is
        /// finished and nothing more is written until it is started again.
        Stopped,
        /// A finished file's tags could not be written. The audio in
        /// it is whole; only the tags are missing. Said before the file's
        /// FileFinished.
        TagsNotWritten,
    };

    enum class StopReason : std::uint8_t {
        TimeLimit, ///< FileLimits::endAfterSeconds
        Silence, ///< FileLimits::Silence::Stop
    };

    Kind kind = Kind::FileStarted;
    /// Which recording, as the caller named it in attach().
    std::uint64_t key = 0;
    std::filesystem::path path;
    std::uint64_t frames = 0;
    std::uint32_t sampleRate = 0;
    FileError error = FileError::WriteFailed;
    std::uint64_t bytesFree = 0;
    StopReason stopReason = StopReason::TimeLimit;
};

struct WriterOptions {
    /// How often a WAV or an AIFF has its length written into its header,
    /// which is how much a crash can cost before the file is repaired.
    std::chrono::milliseconds headerInterval{2000};
    /// How long the writer sleeps when there is nothing to do. The tap holds
    /// seconds, so this only decides how soon audio reaches the disk.
    std::chrono::milliseconds pollInterval{20};
    /// Where the journal of recordings in progress is kept
    /// (RecordingJournal). Empty keeps none.
    std::filesystem::path journalFolder;
    /// What time it is, for naming files. Empty asks the system for the
    /// local time.
    std::function<std::chrono::local_seconds()> clock;
    /// Where each file's dither noise starts, and whether there is any.
    std::uint64_t ditherSeed = FileSpec{}.ditherSeed;
    bool dither = true;

    /// **How much room is left** on the disk a folder is on, in bytes, or
    /// nothing if it cannot be told. Empty asks the file system; a test
    /// says what it likes.
    std::function<std::optional<std::uint64_t>(const std::filesystem::path &)> freeSpace;
    /// Below this, the recording carries on and says so, once a file.
    std::uint64_t lowSpaceBytes = std::uint64_t{1} << 30;
    /// **Below this, the file is finished while it still can be**: closed
    /// properly, with everything before it kept, and the recording stopped
    /// with the reason DiskFull. A disk that runs out in the middle of a
    /// write leaves a header that could not be written and a file that has
    /// to be repaired; stopping short of it leaves neither. A file is not
    /// started with less than this either.
    std::uint64_t reserveBytes = std::uint64_t{64} << 20;
    /// How often the room is looked at while a file is being written.
    std::chrono::milliseconds spaceInterval{500};
};

/// **The thread that puts recordings on the disk.**
///
/// A Recorder block copies each cycle into its tap and goes back to the
/// audio; this drains the taps into files, and may take as long as a disk
/// takes. One writer serves every Recorder of a running session.
///
/// **Everything is asked for, not done.** attach(), start() and finish()
/// queue a command and return; the thread carries it out in order. sync()
/// is the one call that waits: it returns once every command before it has
/// been carried out, which is what a session that is stopping needs to be
/// able to say the files are closed.
///
/// **A recording starts with the audio that arrives after start() is
/// called**, never with what was sitting in the tap from before, and a file
/// is made only when there is audio for it: starting and finishing with
/// nothing in between makes no empty file.
///
/// **Where a command applies is fixed when it is asked for, not when it is
/// carried out.** start() and finish() note how far the tap has got at the
/// moment they are called, and the thread starts or stops the file at
/// exactly that frame however late it gets round to it. A thread that took
/// "now" to mean the moment it carried the command out would throw away
/// the first cycles of a recording whenever it was slow to wake — which,
/// on a busy machine, it was: the session had already put its first audio
/// in the tap by then. Until a recording is started, the newest half of its
/// tap is kept for the same reason.
///
/// **A change of rate starts a new file.** A file has one rate, and the
/// tap says where the new one begins; the new file is named afresh.
///
/// Events are delivered to the sink on the writer's own thread.
class RecordingWriter
{
public:
    using EventSink = std::function<void(const RecordingEvent &)>;

    explicit RecordingWriter(EventSink sink, WriterOptions options = {});
    /// Finishes every recording and stops the thread.
    ~RecordingWriter();

    RecordingWriter(const RecordingWriter &) = delete;
    RecordingWriter &operator=(const RecordingWriter &) = delete;

    /// Starts reading \a tap as the recording called \a key, discarding
    /// what comes out of it until start().
    void attach(std::uint64_t key, std::shared_ptr<rt::AudioTap> tap);
    /// Records what \a key's tap is given from this call on. A recording
    /// that is already going carries on as it was.
    void start(std::uint64_t key, RecordingSettings settings);
    /// **Pause, resume and split**, each at the frame the tap had got
    /// to when it is called, like start() and finish(). A paused recording
    /// keeps its file open and leaves out what arrives until resume(), so
    /// the two parts join with nothing missing from either and nothing
    /// twice. A split ends the file at that frame, and the next frame is the
    /// first of a new one, named with the next counter.
    void pause(std::uint64_t key);
    void resume(std::uint64_t key);
    void split(std::uint64_t key);
    /// Writes out what had arrived when it was called, and closes the file.
    void finish(std::uint64_t key);
    /// Finishes \a key and lets go of its tap.
    void detach(std::uint64_t key);
    /// Waits until every command before it has been carried out, and the
    /// audio that had arrived by then is in its file.
    void sync();

private:
    struct Attach {
        std::uint64_t key;
        std::shared_ptr<rt::AudioTap> tap;
    };
    struct Start {
        std::uint64_t key;
        RecordingSettings settings;
        /// How far the tap had got when start() was called: the first frame
        /// of the recording.
        std::uint64_t from = 0;
    };
    struct Finish {
        std::uint64_t key;
        /// How far the tap had got when finish() was called: the frame the
        /// file ends before.
        std::uint64_t upTo = 0;
    };
    struct Detach {
        std::uint64_t key;
    };
    /// Pause, resume or split, at \a at.
    struct Control {
        enum class Action : std::uint8_t {
            Pause,
            Resume,
            Split
        };
        std::uint64_t key;
        Action action;
        std::uint64_t at = 0;
    };
    struct Sync {
        std::shared_ptr<std::promise<void>> done;
    };
    using Command = std::variant<Attach, Start, Finish, Detach, Sync, Control>;

    struct Recording;

    void post(Command command);
    void run();
    void apply(Command &command);
    /// Writes, or lets go of, what \a recording's tap holds — no further
    /// than \a upTo when it is given.
    void drain(Recording &recording, std::optional<std::uint64_t> upTo = std::nullopt);
    /// How far \a key's tap has got, for a command being asked for.
    [[nodiscard]] std::uint64_t positionOf(std::uint64_t key);
    /// **The first frame a command still waiting to be carried out applies
    /// at**, for \a key, or nothing. A drain goes no further: writing past
    /// a pause that has been asked for, or letting go of audio past a start,
    /// would put the command at the wrong frame.
    [[nodiscard]] std::optional<std::uint64_t> fenceFor(std::uint64_t key);
    void drainAll();
    [[nodiscard]] bool openFile(Recording &recording);
    void closeFile(Recording &recording);
    [[nodiscard]] bool writeToFile(Recording &recording, std::span<const float> samples);
    /// Frames as the tap gave them: the silence rules, then writeFrames().
    void take(Recording &recording, std::span<const float> samples);
    /// Frames to go in the file: the limits, the conversion, the file.
    void writeFrames(Recording &recording, std::span<const float> samples);
    void stopByItself(Recording &recording, RecordingEvent::StopReason reason);
    void checkSpace(Recording &recording);
    [[nodiscard]] std::optional<std::uint64_t> freeSpaceFor(const std::filesystem::path &folder) const;
    void emit(RecordingEvent event);

    EventSink m_sink;
    WriterOptions m_options;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<Command> m_queue;
    bool m_stopping = false;
    /// The taps, by key, for the callers' side: what start() and finish()
    /// read the position off. Under m_mutex.
    std::map<std::uint64_t, std::shared_ptr<rt::AudioTap>> m_taps;

    /// Writer thread only.
    std::map<std::uint64_t, std::unique_ptr<Recording>> m_recordings;
    /// The commands taken off the queue and not carried out yet.
    std::vector<Command> m_batch;
    std::size_t m_batchNext = 0;
    std::vector<float> m_buffer;
    std::vector<float> m_mono;

    std::thread m_thread;
};

} // namespace rb::record
