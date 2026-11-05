// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/fft.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"
#include "record/impulsefile.h"
#include "rt/retirequeue.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// The last spectra of the input, one a partition, as far back as the
/// longest response needs: the frequency-domain delay line. It belongs to
/// the input and not to a response, so a new response starts with the past
/// the old one had and is heard whole at once.
struct FirHistory {
    std::size_t capacity = 0;
    /// Per input channel, capacity spectra of FirFilter::kStride numbers.
    std::array<std::vector<float>, 2> real;
    std::array<std::vector<float>, 2> imaginary;
};

/// An impulse response ready for the audio thread: every partition of it
/// already in the frequency domain.
///
/// Made on the engine thread from a file, handed to the audio thread whole,
/// and freed on the engine thread again once the audio thread has let go of
/// it. Nothing in it changes while the audio thread holds it.
struct FirKernel {
    /// How many partitions long, and what that was in samples.
    std::size_t partitions = 0;
    std::size_t taps = 0;
    /// 1 (the same response on both sides), 2 (one a side) or 4 (true
    /// stereo: left to left, left to right, right to left, right to right).
    std::size_t paths = 0;
    /// Every path's partitions, one after another, each
    /// FirFilter::kStride numbers long, the real and the imaginary parts
    /// apart so that the sum over the partitions runs over plain arrays of
    /// floats.
    std::vector<float> real;
    std::vector<float> imaginary;
    /// What the block publishes while this one is heard: see
    /// FirFilter::kShapeValue.
    float shape = 0.0f;
};

/// Applies an impulse response — a room, a speaker, a headphone correction,
/// a linear-phase filter — by convolution.
///
/// **Uniformly partitioned, overlap-save.** The response is cut into
/// partitions of kPartition samples, each transformed once, on the engine
/// thread, at twice that length. Every kPartition samples of input are
/// transformed too and kept (FirHistory); the output of a partition is the
/// sum, over the response's partitions, of each one times the input
/// kPartition samples further back, and one inverse transform. A 2 s
/// response at 48 kHz is 375 partitions: a few hundred thousand
/// multiplications every 5 ms, where the same convolution in time would be
/// twenty-five times that.
///
/// **The latency is kPartition samples at every quantum**, reported:
/// a partition is heard once all of it has arrived. The partition
/// does not follow the quantum. A graph whose quantum changes would
/// otherwise change this block's latency underneath every path lined up
/// with it, and the output would depend on how the input was cut into
/// cycles, which no other block's does.
///
/// **Loading happens off the audio thread.** Setting "file" reads it
/// (libsndfile), resamples it to the graph's rate (soxr), trims and
/// normalises it and transforms every partition, all on the engine thread
/// that set it. The finished FirKernel is handed over through one atomic
/// pointer; the audio thread takes it at the next partition and crossfades
/// from the old response's output to the new one's over kFadeFrames, so a
/// change is heard without a click, then hands the old one back through a
/// retire queue to be freed where freeing is allowed.
///
/// Without a response, or with a file it cannot read, it passes the sound
/// through, still kPartition samples late.
class FirFilter final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.firfilter";

    static constexpr std::size_t kPartition = 256;
    static constexpr std::size_t kFftSize = kPartition * 2;
    static constexpr std::size_t kBins = kFftSize / 2 + 1;
    /// A partition's spectrum as stored: kBins rounded up to eight, so that
    /// every one starts aligned for the vector unit.
    static constexpr std::size_t kStride = (kBins + 7) / 8 * 8;
    /// The longest response kept, at any rate. Longer ones are cut there
    /// with a short fade. A longer one wants partitions that grow along it
    /// (non-uniform partitioning), which is for later.
    static constexpr double kMaxSeconds = 10.0;
    /// How long a change of response takes to cross over: four partitions,
    /// 21 ms at 48 kHz.
    static constexpr std::size_t kFadeFrames = kPartition * 4;
    /// Trimming takes off what is this far under the response's peak at
    /// either end.
    static constexpr float kTrimDb = -80.0f;
    static constexpr float kLowestPreGainDb = -24.0f;
    static constexpr float kPreGainSmoothingMs = 20.0f;

    /// What the block publishes (rt::BlockMeter): the shape of the response
    /// it is using, how long that is in samples at the running rate and in
    /// seconds, and its latency in milliseconds.
    static constexpr std::size_t kShapeValue = 0;
    static constexpr std::size_t kTapsValue = 1;
    static constexpr std::size_t kSecondsValue = 2;
    static constexpr std::size_t kDelayMsValue = 3;
    /// The shapes: no file; a file it cannot use; and otherwise how many
    /// channels the response has — 1, 2 or 4.
    static constexpr float kNoFile = 0.0f;
    static constexpr float kUnusable = -1.0f;

    enum Param : std::uint32_t {
        PreGain = 0, ///< dB, before the convolution.
        File, ///< The impulse response's path.
        Normalise, ///< Whether the response is scaled so that noise comes out as loud as it went in.
        Trim, ///< Whether silence at either end is taken off.
    };

    FirFilter();
    ~FirFilter() override;
    FirFilter(const FirFilter &) = delete;
    FirFilter &operator=(const FirFilter &) = delete;

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// \a channels (1, 2 or 4) of a response at \a sampleRate, as the block
    /// uses them: cut to kMaxSeconds, trimmed and normalised as asked, and
    /// transformed. Nothing if the channels are not a shape the block can
    /// use. Public so that the tests can compare against the same numbers.
    [[nodiscard]] static std::unique_ptr<FirKernel>
    makeKernel(std::vector<std::vector<float>> channels, double sampleRate, bool normalise, bool trim);

    /// \a channels as the block convolves with them, in time: cut, trimmed
    /// and normalised, before anything is transformed.
    [[nodiscard]] static std::vector<std::vector<float>>
    shapeResponse(std::vector<std::vector<float>> channels, double sampleRate, bool normalise, bool trim);

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// The shape of the response being heard, as kShapeValue publishes it.
    [[nodiscard]] float shape() const noexcept RB_NONBLOCKING;
    /// Its length in samples at the running rate.
    [[nodiscard]] std::size_t taps() const noexcept RB_NONBLOCKING;
    /// Whether a new response is on its way in or still crossing over.
    [[nodiscard]] bool changing() const noexcept RB_NONBLOCKING
    {
        return m_incoming != nullptr || m_pending.load(std::memory_order_acquire) != nullptr;
    }

private:
    static constexpr std::size_t kChannels = 2;

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    /// Reads the file "file" names into m_source, or records why not.
    void readFile();
    /// A response from m_source at the running rate, with the settings as
    /// they are. Never null: without a usable file it is the one that
    /// passes the sound through.
    [[nodiscard]] std::unique_ptr<FirKernel> buildKernel() const;
    /// Builds the response again and gives it to the audio thread, or puts
    /// it in place if nothing is running the block.
    void rebuild();
    /// Frees what the audio thread has finished with.
    void collect();

    /// One partition: transform the input, take it through the response(s)
    /// and back, into m_ready.
    void processPartition() noexcept RB_NONBLOCKING;
    /// The sum over \a kernel's partitions of the input's history times the
    /// response, for output \a output, into m_sumReal / m_sumImaginary.
    void convolve(const FirKernel &kernel, std::size_t output) noexcept RB_NONBLOCKING;
    /// Takes a response handed over, and a longer history, if there is one
    /// and room to retire what they replace.
    void takePending() noexcept RB_NONBLOCKING;
    /// \a history in place of the one the audio thread has, with every
    /// spectrum of the old one in it, newest last.
    void adoptHistory(FirHistory *history) noexcept RB_NONBLOCKING;
    void publish() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::RealFft m_fft;
    dsp::LinearSmoother m_preGain;
    rt::BlockMeter *m_meter = nullptr;
    double m_sampleRate = 48000.0;
    bool m_prepared = false;

    // --- the engine thread's side ---

    /// The file as it was read, at its own rate, so that a new rate or a
    /// new setting does not read it again.
    std::optional<record::ImpulseFile> m_source;
    /// The file m_source came from, and whether reading it failed.
    std::string m_sourcePath;
    bool m_unusable = false;
    /// How many partitions of input the audio thread keeps, or will once
    /// it has taken the history on its way to it. Only ever grows.
    std::size_t m_historyCapacity = 0;

    // --- the handover ---

    /// A response on its way to the audio thread; the audio thread takes it
    /// by exchanging in null. One the audio thread never took is freed by
    /// the engine thread when it sends the next.
    std::atomic<FirKernel *> m_pending{nullptr};
    /// A longer history, sent before a response that needs it. **Taken
    /// after the response**: whoever sees the response then sees the history
    /// that was stored before it, so a response is never used with a
    /// history too short for it.
    std::atomic<FirHistory *> m_pendingHistory{nullptr};
    /// What the audio thread has let go of: responses and histories.
    rt::RetireQueue m_retired{16};

    // --- the audio thread's side ---

    /// **Plain pointers**, owned by the block: the audio thread hands them
    /// on and never frees them, and a std::unique_ptr would give it a
    /// destructor to call. Freed by the engine thread in prepare() and the
    /// destructor, when nothing is running the block.
    FirKernel *m_kernel = nullptr;
    /// The response being crossed over to, and how far.
    FirKernel *m_incoming = nullptr;
    std::size_t m_fadePosition = 0;
    FirHistory *m_history = nullptr;
    /// Where the newest spectrum is in the history.
    std::size_t m_head = 0;

    /// Per channel: the last two partitions of input (the older half first),
    /// and the partition of output being played.
    std::array<std::vector<float>, kChannels> m_input;
    std::array<std::vector<float>, kChannels> m_ready;
    /// How much of the current partition has arrived.
    std::size_t m_fill = 0;

    std::vector<float> m_frame;
    std::vector<std::complex<float>> m_spectrum;
    std::vector<float> m_sumReal;
    std::vector<float> m_sumImaginary;
};

} // namespace rb::blocks
