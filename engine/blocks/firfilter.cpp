// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/firfilter.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

/// How long the fade is where a response longer than kMaxSeconds is cut.
constexpr double kCutFadeSeconds = 0.01;

/// Whether a file of \a channels is a shape the block convolves with.
[[nodiscard]] bool usableShape(std::size_t channels) noexcept
{
    return channels == 1 || channels == 2 || channels == 4;
}

/// A history of \a capacity spectra, all silence.
[[nodiscard]] std::unique_ptr<FirHistory> makeHistory(std::size_t capacity)
{
    auto history = std::make_unique<FirHistory>();
    history->capacity = std::max<std::size_t>(capacity, 1);
    for (std::size_t channel = 0; channel < history->real.size(); ++channel) {
        history->real[channel].assign(history->capacity * FirFilter::kStride, 0.0f);
        history->imaginary[channel].assign(history->capacity * FirFilter::kStride, 0.0f);
    }
    return history;
}

/// The response that is the sound itself, kPartition samples late: one
/// partition whose spectrum is 1 everywhere.
[[nodiscard]] std::unique_ptr<FirKernel> passThrough(float shape)
{
    auto kernel = std::make_unique<FirKernel>();
    kernel->partitions = 1;
    kernel->taps = 1;
    kernel->paths = 1;
    kernel->real.assign(FirFilter::kStride, 1.0f);
    kernel->imaginary.assign(FirFilter::kStride, 0.0f);
    kernel->shape = shape;
    return kernel;
}

} // namespace

std::vector<ParamDescriptor> FirFilter::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor preGain;
    preGain.id = "preGainDb";
    preGain.type = ParamType::Float;
    preGain.minimum = ParamValue::ofFloat(kLowestPreGainDb);
    preGain.maximum = ParamValue::ofFloat(0.0f);
    preGain.defaultValue = ParamValue::ofFloat(0.0f);
    preGain.unit = ParamUnit::Decibels;
    preGain.smoothingMs = kPreGainSmoothingMs;
    descriptors.push_back(std::move(preGain));

    // **Applied by the block itself, on the thread that sets it**: reading
    // the file and transforming it is work for the engine thread, and what
    // reaches the audio thread is the finished response.
    ParamDescriptor file;
    file.id = "file";
    file.type = ParamType::String;
    file.defaultValue = ParamValue::ofString(std::string{});
    file.flags.hidden = true;
    descriptors.push_back(std::move(file));

    ParamDescriptor normalise;
    normalise.id = "normalise";
    normalise.type = ParamType::Bool;
    normalise.defaultValue = ParamValue::ofBool(true);
    descriptors.push_back(std::move(normalise));

    ParamDescriptor trim;
    trim.id = "trim";
    trim.type = ParamType::Bool;
    trim.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(trim));
    return descriptors;
}

FirFilter::FirFilter()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.latencySamples = static_cast<std::uint32_t>(kPartition);
}

FirFilter::~FirFilter()
{
    delete m_pending.exchange(nullptr, std::memory_order_acq_rel);
    delete m_pendingHistory.exchange(nullptr, std::memory_order_acq_rel);
    delete m_kernel;
    delete m_incoming;
    delete m_history;
}

std::unique_ptr<Block> FirFilter::make()
{
    return std::make_unique<FirFilter>();
}

std::vector<std::vector<float>>
FirFilter::shapeResponse(std::vector<std::vector<float>> channels, double sampleRate, bool normalise, bool trim)
{
    if (channels.empty() || channels.front().empty()) {
        return channels;
    }
    std::size_t length = channels.front().size();

    if (trim) {
        // **Both ends, and every channel alike**, so that the channels stay
        // lined up with each other.
        float peak = 0.0f;
        for (const std::vector<float> &channel : channels) {
            for (const float sample : channel) {
                peak = std::max(peak, std::abs(sample));
            }
        }
        const float threshold = peak * dbToGain(kTrimDb);
        std::size_t first = length;
        std::size_t last = 0;
        for (const std::vector<float> &channel : channels) {
            for (std::size_t n = 0; n < length; ++n) {
                if (std::abs(channel[n]) > threshold) {
                    first = std::min(first, n);
                    last = std::max(last, n);
                }
            }
        }
        if (first < length) {
            for (std::vector<float> &channel : channels) {
                channel.erase(channel.begin() + static_cast<std::ptrdiff_t>(last + 1), channel.end());
                channel.erase(channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(first));
            }
            length = last + 1 - first;
        }
    }

    const auto longest = static_cast<std::size_t>(kMaxSeconds * sampleRate);
    if (length > longest) {
        // A cut in the middle of a tail is a step; a fade makes it the end
        // of the tail instead.
        const auto fade = std::min(longest, static_cast<std::size_t>(kCutFadeSeconds * sampleRate));
        for (std::vector<float> &channel : channels) {
            channel.resize(longest);
            for (std::size_t n = 0; n < fade; ++n) {
                const double x = static_cast<double>(n + 1) / static_cast<double>(fade);
                channel[longest - 1 - n] *= static_cast<float>(0.5 - 0.5 * std::cos(std::numbers::pi * x));
            }
        }
    }

    if (normalise) {
        // **So that noise comes out as loud as it goes in**: the energy of
        // everything that reaches one output, at the louder output, is made
        // one. A single click at full scale is left as it is.
        std::vector<double> energy(channels.size(), 0.0);
        for (std::size_t path = 0; path < channels.size(); ++path) {
            for (const float sample : channels[path]) {
                energy[path] += static_cast<double>(sample) * static_cast<double>(sample);
            }
        }
        double loudest = 0.0;
        if (channels.size() == 4) {
            loudest = std::max(energy[0] + energy[2], energy[1] + energy[3]);
        } else {
            loudest = *std::ranges::max_element(energy);
        }
        if (loudest > 0.0) {
            const auto scale = static_cast<float>(1.0 / std::sqrt(loudest));
            for (std::vector<float> &channel : channels) {
                for (float &sample : channel) {
                    sample *= scale;
                }
            }
        }
    }
    return channels;
}

std::unique_ptr<FirKernel>
FirFilter::makeKernel(std::vector<std::vector<float>> channels, double sampleRate, bool normalise, bool trim)
{
    if (!usableShape(channels.size())) {
        return nullptr;
    }
    const std::vector<std::vector<float>> response = shapeResponse(std::move(channels), sampleRate, normalise, trim);
    const std::size_t taps = response.front().size();
    if (taps == 0) {
        return nullptr;
    }

    auto kernel = std::make_unique<FirKernel>();
    kernel->taps = taps;
    kernel->paths = response.size();
    kernel->partitions = (taps + kPartition - 1) / kPartition;
    kernel->shape = static_cast<float>(response.size());
    kernel->real.assign(kernel->paths * kernel->partitions * kStride, 0.0f);
    kernel->imaginary.assign(kernel->paths * kernel->partitions * kStride, 0.0f);

    RealFft fft;
    fft.prepare(kFftSize);
    std::vector<float> frame(kFftSize, 0.0f);
    std::vector<std::complex<float>> spectrum(kBins);
    for (std::size_t path = 0; path < kernel->paths; ++path) {
        for (std::size_t partition = 0; partition < kernel->partitions; ++partition) {
            // A partition of the response, then as many zeros: overlap-save
            // keeps the half of every product that has no wrap-around in it.
            std::ranges::fill(frame, 0.0f);
            const std::size_t from = partition * kPartition;
            const std::size_t count = std::min(kPartition, taps - from);
            std::copy_n(response[path].begin() + static_cast<std::ptrdiff_t>(from), count, frame.begin());
            fft.forward(frame, spectrum);
            const std::size_t at = (path * kernel->partitions + partition) * kStride;
            for (std::size_t bin = 0; bin < kBins; ++bin) {
                kernel->real[at + bin] = spectrum[bin].real();
                kernel->imaginary[at + bin] = spectrum[bin].imag();
            }
        }
    }
    return kernel;
}

void FirFilter::readFile()
{
    const std::string path = m_store.value(File).text();
    m_sourcePath = path;
    m_source.reset();
    m_unusable = false;
    if (path.empty()) {
        return;
    }
    std::expected<record::ImpulseFile, record::ImpulseFileError> read = record::readImpulseFile(path);
    if (!read || !usableShape(read->channels.size())) {
        m_unusable = true;
        return;
    }
    m_source = std::move(*read);
}

std::unique_ptr<FirKernel> FirFilter::buildKernel() const
{
    if (!m_source) {
        return passThrough(m_unusable ? kUnusable : kNoFile);
    }
    const auto rate = static_cast<std::uint32_t>(std::lround(m_sampleRate));
    std::unique_ptr<FirKernel> kernel = makeKernel(record::resampleImpulse(*m_source, rate),
                                                   m_sampleRate,
                                                   m_store.number(Normalise) >= 0.5f,
                                                   m_store.number(Trim) >= 0.5f);
    return kernel ? std::move(kernel) : passThrough(kUnusable);
}

void FirFilter::collect()
{
    (void)m_retired.drain();
}

void FirFilter::rebuild()
{
    collect();
    std::unique_ptr<FirKernel> kernel = buildKernel();
    // **The history first**, if the new response reaches further back than
    // the audio thread remembers. One that was never taken is smaller than
    // this one, so it can go.
    if (kernel->partitions > m_historyCapacity) {
        m_historyCapacity = kernel->partitions;
        delete m_pendingHistory.exchange(makeHistory(m_historyCapacity).release(), std::memory_order_acq_rel);
    }
    // A response the audio thread never took was never heard: this one
    // replaces it.
    delete m_pending.exchange(kernel.release(), std::memory_order_acq_rel);
}

std::expected<void, BlockError> FirFilter::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_meter = spec.meter;

    // Nothing runs the block while it is prepared: whatever was on its way
    // is out of date, and the response is put in place directly.
    collect();
    delete m_pending.exchange(nullptr, std::memory_order_acq_rel);
    delete m_pendingHistory.exchange(nullptr, std::memory_order_acq_rel);
    delete m_incoming;
    m_incoming = nullptr;
    m_fadePosition = 0;

    m_fft.prepare(kFftSize);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        m_input[channel].assign(kFftSize, 0.0f);
        m_ready[channel].assign(kPartition, 0.0f);
    }
    m_frame.assign(kFftSize, 0.0f);
    m_spectrum.assign(kBins, {0.0f, 0.0f});
    m_sumReal.assign(kStride, 0.0f);
    m_sumImaginary.assign(kStride, 0.0f);

    delete m_kernel;
    m_kernel = buildKernel().release();
    m_historyCapacity = m_kernel->partitions;
    delete m_history;
    m_history = makeHistory(m_historyCapacity).release();
    m_head = 0;
    m_fill = 0;

    m_preGain.prepare(spec.sampleRate, kPreGainSmoothingMs);
    m_preGain.reset(dbToGain(m_store.number(PreGain)));
    m_prepared = true;
    return {};
}

void FirFilter::adoptHistory(FirHistory *history) noexcept RB_NONBLOCKING
{
    // The old spectra at the end of the new history, newest last, and the
    // silence of the longer past before them.
    const std::size_t kept = std::min(m_history->capacity, history->capacity);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        for (std::size_t age = 0; age < kept; ++age) {
            const std::size_t from = (m_head + m_history->capacity - age) % m_history->capacity * kStride;
            const std::size_t to = (kept - 1 - age) * kStride;
            std::copy_n(m_history->real[channel].begin() + static_cast<std::ptrdiff_t>(from),
                        kStride,
                        history->real[channel].begin() + static_cast<std::ptrdiff_t>(to));
            std::copy_n(m_history->imaginary[channel].begin() + static_cast<std::ptrdiff_t>(from),
                        kStride,
                        history->imaginary[channel].begin() + static_cast<std::ptrdiff_t>(to));
        }
    }
    m_head = kept - 1;
    std::swap(m_history, history);
    // takePending() made sure there is room.
    (void)m_retired.tryRetire(history);
}

void FirFilter::takePending() noexcept RB_NONBLOCKING
{
    // Room for the history and the response this may retire, or it waits:
    // freeing either here is what the queue is for not doing.
    if (m_incoming != nullptr || m_retired.pending() + 2 > m_retired.capacity()) {
        return;
    }
    FirKernel *const kernel = m_pending.exchange(nullptr, std::memory_order_acq_rel);
    FirHistory *const history = m_pendingHistory.exchange(nullptr, std::memory_order_acq_rel);
    if (history != nullptr) {
        adoptHistory(history);
    }
    if (kernel != nullptr) {
        m_incoming = kernel;
        m_fadePosition = 0;
    }
}

void FirFilter::convolve(const FirKernel &kernel, std::size_t output) noexcept RB_NONBLOCKING
{
    float *const sumReal = m_sumReal.data();
    float *const sumImaginary = m_sumImaginary.data();
    std::fill_n(sumReal, kStride, 0.0f);
    std::fill_n(sumImaginary, kStride, 0.0f);

    // Which input goes through which path to this output: the same response
    // on both sides, one a side, or both inputs through their own to each.
    struct Term {
        std::size_t input;
        std::size_t path;
    };
    std::array<Term, 2> terms{};
    std::size_t count = 1;
    if (kernel.paths == 1) {
        terms[0] = {output, 0};
    } else if (kernel.paths == 2) {
        terms[0] = {output, output};
    } else {
        terms[0] = {0, output};
        terms[1] = {1, 2 + output};
        count = 2;
    }

    const std::size_t capacity = m_history->capacity;
    const std::size_t partitions = std::min(kernel.partitions, capacity);
    for (std::size_t term = 0; term < count; ++term) {
        const float *const historyReal = m_history->real[terms[term].input].data();
        const float *const historyImaginary = m_history->imaginary[terms[term].input].data();
        const float *const kernelReal = kernel.real.data() + terms[term].path * kernel.partitions * kStride;
        const float *const kernelImaginary = kernel.imaginary.data() + terms[term].path * kernel.partitions * kStride;
        std::size_t slot = m_head;
        for (std::size_t partition = 0; partition < partitions; ++partition) {
            const float *const xr = historyReal + slot * kStride;
            const float *const xi = historyImaginary + slot * kStride;
            const float *const hr = kernelReal + partition * kStride;
            const float *const hi = kernelImaginary + partition * kStride;
            for (std::size_t bin = 0; bin < kStride; ++bin) {
                sumReal[bin] += xr[bin] * hr[bin] - xi[bin] * hi[bin];
                sumImaginary[bin] += xr[bin] * hi[bin] + xi[bin] * hr[bin];
            }
            slot = slot == 0 ? capacity - 1 : slot - 1;
        }
    }
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        m_spectrum[bin] = {sumReal[bin], sumImaginary[bin]};
    }
    m_fft.inverse(m_spectrum, m_frame);
}

void FirFilter::processPartition() noexcept RB_NONBLOCKING
{
    takePending();

    // The last two partitions of input into the history, as the newest.
    m_head = (m_head + 1) % m_history->capacity;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        m_fft.forward(m_input[channel], m_spectrum);
        float *const real = m_history->real[channel].data() + m_head * kStride;
        float *const imaginary = m_history->imaginary[channel].data() + m_head * kStride;
        for (std::size_t bin = 0; bin < kBins; ++bin) {
            real[bin] = m_spectrum[bin].real();
            imaginary[bin] = m_spectrum[bin].imag();
        }
        std::copy(m_input[channel].begin() + kPartition, m_input[channel].end(), m_input[channel].begin());
    }

    for (std::size_t output = 0; output < kChannels; ++output) {
        // The second half of what comes back is the partition's output;
        // the first is wrapped around and thrown away.
        convolve(*m_kernel, output);
        std::copy(m_frame.begin() + kPartition, m_frame.end(), m_ready[output].begin());
        if (m_incoming != nullptr) {
            convolve(*m_incoming, output);
            for (std::size_t n = 0; n < kPartition; ++n) {
                const float into =
                    std::min(1.0f, static_cast<float>(m_fadePosition + n + 1) / static_cast<float>(kFadeFrames));
                m_ready[output][n] += (m_frame[kPartition + n] - m_ready[output][n]) * into;
            }
        }
    }

    if (m_incoming != nullptr) {
        m_fadePosition += kPartition;
        // Over once the new one is all that is heard and the old one has
        // somewhere to go; takePending() made sure it has.
        if (m_fadePosition >= kFadeFrames && m_retired.tryRetire(m_kernel)) {
            m_kernel = std::exchange(m_incoming, nullptr);
            m_fadePosition = 0;
        }
    }
}

void FirFilter::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_preGain.setTarget(dbToGain(m_store.number(PreGain)));

    std::array<std::span<float>, kChannels> in;
    std::array<std::span<float>, kChannels> out;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        in[channel] = context.input.channel(static_cast<std::uint32_t>(channel));
        out[channel] = context.output.channel(static_cast<std::uint32_t>(channel));
    }
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const float gain = m_preGain.next();
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            // Read before written: the graph may hand one buffer for both.
            const float sample = frame < in[channel].size() ? in[channel][frame] : 0.0f;
            if (frame < out[channel].size()) {
                out[channel][frame] = m_ready[channel][m_fill];
            }
            m_input[channel][kPartition + m_fill] = sample * gain;
        }
        if (++m_fill == kPartition) {
            processPartition();
            m_fill = 0;
        }
    }
    publish();
}

void FirFilter::publish() noexcept RB_NONBLOCKING
{
    if (m_meter == nullptr) {
        return;
    }
    // What is being crossed over to is what the face should say.
    const FirKernel &heard = m_incoming != nullptr ? *m_incoming : *m_kernel;
    const bool response = heard.shape > 0.0f;
    m_meter->publish(kShapeValue, heard.shape);
    m_meter->publish(kTapsValue, response ? static_cast<float>(heard.taps) : 0.0f);
    m_meter->publish(kSecondsValue,
                     response ? static_cast<float>(static_cast<double>(heard.taps) / m_sampleRate) : 0.0f);
    m_meter->publish(kDelayMsValue, static_cast<float>(static_cast<double>(kPartition) * 1000.0 / m_sampleRate));
}

float FirFilter::shape() const noexcept RB_NONBLOCKING
{
    return m_kernel != nullptr ? m_kernel->shape : kNoFile;
}

std::size_t FirFilter::taps() const noexcept RB_NONBLOCKING
{
    return m_kernel != nullptr && m_kernel->shape > 0.0f ? m_kernel->taps : 0;
}

void FirFilter::reset() noexcept RB_NONBLOCKING
{
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        std::ranges::fill(m_input[channel], 0.0f);
        std::ranges::fill(m_ready[channel], 0.0f);
    }
    if (m_history != nullptr) {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            std::ranges::fill(m_history->real[channel], 0.0f);
            std::ranges::fill(m_history->imaginary[channel], 0.0f);
        }
    }
    m_head = 0;
    m_fill = 0;
    // Nothing is left to cross over from.
    if (m_incoming != nullptr && m_retired.tryRetire(m_kernel)) {
        m_kernel = std::exchange(m_incoming, nullptr);
        m_fadePosition = 0;
    }
    m_preGain.reset(dbToGain(m_store.number(PreGain)));
}

void FirFilter::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // The pre-gain is read at the top of every cycle. Normalise and trim
    // arrive here too, but what they change was built on the engine thread
    // when they were set, and arrives as a response.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> FirFilter::setParam(std::string_view id, const ParamValue &value)
{
    const float normaliseBefore = m_store.number(Normalise);
    const float trimBefore = m_store.number(Trim);
    const std::expected<void, BlockError> set = m_store.set(id, value);
    if (!set) {
        return set;
    }
    if (id == "file") {
        // **Read again even when it is the same path**: choosing the file
        // again is how somebody says it has changed on the disk.
        readFile();
    } else if (!((id == "normalise" && m_store.number(Normalise) != normaliseBefore)
                 || (id == "trim" && m_store.number(Trim) != trimBefore))) {
        return set;
    }
    if (m_prepared) {
        rebuild();
    }
    return set;
}

ParamValue FirFilter::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap FirFilter::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> FirFilter::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (!loaded) {
        return loaded;
    }
    if (m_store.value(File).text() != m_sourcePath || (!m_source && !m_sourcePath.empty())) {
        readFile();
    }
    if (m_prepared) {
        rebuild();
    }
    return loaded;
}

} // namespace rb::blocks
