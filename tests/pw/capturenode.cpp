// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "capturenode.h"

#include <pipewire/filter.h>
#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <thread>

namespace rb::pwtest
{

namespace
{

void raiseTo(std::atomic<float> &target, float value)
{
    float seen = target.load(std::memory_order_relaxed);
    while (value > seen && !target.compare_exchange_weak(seen, value, std::memory_order_relaxed)) { }
}

struct PortMeter {
    std::string channel;
    void *port = nullptr; // the filter's port data
    std::atomic<float> peak{0.0f};
    std::atomic<bool> ran{false};
    // Sine check. The history belongs to the data thread; reset asks it to
    // start over, because the samples before a reset don't count.
    std::atomic<float> residual{0.0f};
    std::atomic<std::uint64_t> samples{0};
    std::atomic<std::uint64_t> nullBuffers{0};
    std::atomic<std::uint64_t> cycles{0};
    std::atomic<std::uint64_t> clickedCycles{0};
    std::atomic<bool> resetHistory{true};
    float previous1 = 0.0f;
    float previous2 = 0.0f;
    int history = 0;

    // One frequency's amplitude, accumulated as the signal goes by: the sum
    // of x·cos(wn) and x·sin(wn) over a window is the Fourier coefficient at
    // that frequency, and needs no samples kept. The phase belongs to the
    // data thread; the sums are read by whoever measures.
    std::atomic<double> toneCos{0.0};
    std::atomic<double> toneSin{0.0};
    std::atomic<std::uint64_t> toneSamples{0};
    double tonePhase = 0.0;
};

} // namespace

struct CaptureNode::Impl {
    pw_thread_loop *loop = nullptr;
    pw_filter *filter = nullptr;
    spa_hook listener{};
    std::vector<std::unique_ptr<PortMeter>> meters;
    std::atomic<std::uint64_t> cycles{0};
    std::atomic<std::uint64_t> lostCycles{0};
    std::uint64_t expectedPosition = 0; // data thread only
    float twoCosW = 0.0f; // 2cos(w) of the expected sine; 0 when unchecked
    float clickResidual = 0.0f; // a cycle's residual above this is a click
    bool checkSine = false;
    /// Radians per sample of the frequency measureTone() is looking for,
    /// zero when nobody has asked for one.
    std::atomic<double> toneStep{0.0};

    static void onProcess(void *data, spa_io_position *position)
    {
        auto *self = static_cast<Impl *>(data);
        const auto frames = position ? static_cast<std::uint32_t>(position->clock.duration) : 0u;
        if (position) {
            // A cycle that doesn't start where the previous one ended means
            // this node missed cycles, so the captured samples have a gap.
            const std::uint64_t at = position->clock.position;
            if (self->expectedPosition != 0 && at != self->expectedPosition) {
                self->lostCycles.fetch_add(1, std::memory_order_relaxed);
            }
            self->expectedPosition = at + frames;
        }
        for (const auto &meter : self->meters) {
            const auto *samples = static_cast<const float *>(pw_filter_get_dsp_buffer(meter->port, frames));
            if (meter->resetHistory.exchange(false, std::memory_order_acquire)) {
                meter->history = 0;
            }
            if (!samples) {
                meter->nullBuffers.fetch_add(1, std::memory_order_relaxed);
                meter->history = 0;
                continue;
            }
            float peak = 0.0f;
            float residual = 0.0f;
            const double toneStep = self->toneStep.load(std::memory_order_relaxed);
            double toneCos = 0.0;
            double toneSin = 0.0;
            double phase = meter->tonePhase;
            for (std::uint32_t i = 0; i < frames; ++i) {
                const float x = samples[i];
                peak = std::max(peak, std::abs(x));
                if (toneStep != 0.0) {
                    toneCos += static_cast<double>(x) * std::cos(phase);
                    toneSin += static_cast<double>(x) * std::sin(phase);
                    phase += toneStep;
                }
                if (self->checkSine) {
                    if (meter->history >= 2) {
                        residual =
                            std::max(residual, std::abs(x - self->twoCosW * meter->previous1 + meter->previous2));
                    } else {
                        ++meter->history;
                    }
                    meter->previous2 = meter->previous1;
                    meter->previous1 = x;
                }
            }
            if (toneStep != 0.0) {
                // The phase carries on across cycles, so a window can be any
                // length and still measure the same frequency.
                meter->tonePhase = std::fmod(phase, 2.0 * std::numbers::pi);
                meter->toneCos.store(meter->toneCos.load(std::memory_order_relaxed) + toneCos,
                                     std::memory_order_relaxed);
                meter->toneSin.store(meter->toneSin.load(std::memory_order_relaxed) + toneSin,
                                     std::memory_order_relaxed);
                meter->toneSamples.fetch_add(frames, std::memory_order_relaxed);
            }
            raiseTo(meter->peak, peak);
            raiseTo(meter->residual, residual);
            if (self->checkSine && residual > self->clickResidual) {
                meter->clickedCycles.fetch_add(1, std::memory_order_relaxed);
            }
            meter->cycles.fetch_add(1, std::memory_order_relaxed);
            meter->samples.fetch_add(frames, std::memory_order_relaxed);
            meter->ran.store(true, std::memory_order_relaxed);
        }
        self->cycles.fetch_add(1, std::memory_order_relaxed);
    }
};

CaptureNode::CaptureNode(CaptureNodeOptions options)
    : m_options(std::move(options))
    , m_impl(std::make_unique<Impl>())
{
    static const bool initialized = [] {
        pw_init(nullptr, nullptr);
        return true;
    }();
    (void)initialized;

    static const pw_filter_events events = [] {
        pw_filter_events result{};
        result.version = PW_VERSION_FILTER_EVENTS;
        result.process = &Impl::onProcess;
        return result;
    }();

    if (m_options.sineHz > 0.0) {
        m_impl->checkSine = true;
        m_impl->twoCosW = static_cast<float>(2.0 * std::cos(2.0 * std::numbers::pi * m_options.sineHz / 48000.0));
        m_impl->clickResidual = static_cast<float>(std::pow(10.0, m_options.clickResidualDb / 20.0));
    }
    m_impl->loop = pw_thread_loop_new("rbtest-capture", nullptr);
    pw_properties *props = pw_properties_new(PW_KEY_NODE_NAME,
                                             m_options.name.c_str(),
                                             PW_KEY_MEDIA_TYPE,
                                             "Audio",
                                             PW_KEY_NODE_AUTOCONNECT,
                                             "false",
                                             PW_KEY_APP_NAME,
                                             "rbtest-capture",
                                             nullptr);
    if (!m_options.mediaClass.empty()) {
        pw_properties_set(props, PW_KEY_MEDIA_CLASS, m_options.mediaClass.c_str());
    }
    for (const auto &[key, value] : m_options.props) {
        pw_properties_set(props, key.c_str(), value.c_str());
    }

    pw_thread_loop_lock(m_impl->loop);
    m_impl->filter = pw_filter_new_simple(
        pw_thread_loop_get_loop(m_impl->loop), m_options.name.c_str(), props, &events, m_impl.get());
    for (const std::string &channel : m_options.channels) {
        auto meter = std::make_unique<PortMeter>();
        meter->channel = channel;
        const std::string portName = "input_" + channel;
        meter->port = pw_filter_add_port(m_impl->filter,
                                         PW_DIRECTION_INPUT,
                                         PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                         0,
                                         pw_properties_new(PW_KEY_FORMAT_DSP,
                                                           "32 bit float mono audio",
                                                           PW_KEY_PORT_NAME,
                                                           portName.c_str(),
                                                           PW_KEY_AUDIO_CHANNEL,
                                                           channel.c_str(),
                                                           nullptr),
                                         nullptr,
                                         0);
        m_impl->meters.push_back(std::move(meter));
    }
    const int error = pw_filter_connect(m_impl->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0);
    pw_thread_loop_unlock(m_impl->loop);
    EXPECT_EQ(error, 0) << "pw_filter_connect failed";
    pw_thread_loop_start(m_impl->loop);
}

CaptureNode::~CaptureNode()
{
    pw_thread_loop_stop(m_impl->loop);
    pw_filter_destroy(m_impl->filter);
    pw_thread_loop_destroy(m_impl->loop);
}

std::optional<Graph::Node> CaptureNode::waitUntilReady(std::chrono::milliseconds timeout) const
{
    const auto node = waitForNode(nodeNamed(m_options.name), timeout);
    if (!node || !waitForPorts(node->id, "input", m_options.channels.size(), timeout)) {
        return std::nullopt;
    }
    return node;
}

std::map<std::string, double> CaptureNode::measure(std::chrono::milliseconds duration)
{
    for (const auto &meter : m_impl->meters) {
        meter->peak.store(0.0f);
        meter->ran.store(false);
    }
    std::this_thread::sleep_for(duration);
    std::map<std::string, double> result;
    for (const auto &meter : m_impl->meters) {
        if (!meter->ran.load()) {
            continue;
        }
        const float peak = meter->peak.load();
        result[meter->channel] = peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak)) : -200.0;
    }
    return result;
}

std::map<std::string, double> CaptureNode::measureSettled(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::map<std::string, double> previous = measure(200ms);
    for (;;) {
        std::map<std::string, double> current = measure(200ms);
        const bool same = current.size() == previous.size() && std::ranges::all_of(current, [&](const auto &entry) {
                              const auto it = previous.find(entry.first);
                              return it != previous.end() && std::abs(it->second - entry.second) < 0.05;
                          });
        if (same || std::chrono::steady_clock::now() >= deadline) {
            return current;
        }
        previous = std::move(current);
    }
}

std::map<std::string, SineCheck> CaptureNode::measureSine(std::chrono::milliseconds duration)
{
    EXPECT_TRUE(m_impl->checkSine) << "measureSine() needs CaptureNodeOptions::sineHz";
    for (const auto &meter : m_impl->meters) {
        meter->peak.store(0.0f);
        meter->residual.store(0.0f);
        meter->samples.store(0);
        meter->nullBuffers.store(0);
        meter->cycles.store(0);
        meter->clickedCycles.store(0);
        meter->resetHistory.store(true, std::memory_order_release);
    }
    m_impl->lostCycles.store(0);
    std::this_thread::sleep_for(duration);
    const auto toDb = [](float value) { return value > 0.0f ? 20.0 * std::log10(static_cast<double>(value)) : -200.0; };
    std::map<std::string, SineCheck> result;
    for (const auto &meter : m_impl->meters) {
        result[meter->channel] = {toDb(meter->peak.load()),
                                  toDb(meter->residual.load()),
                                  meter->samples.load(),
                                  meter->nullBuffers.load(),
                                  m_impl->lostCycles.load(),
                                  meter->cycles.load(),
                                  meter->clickedCycles.load()};
    }
    return result;
}

std::map<std::string, double> CaptureNode::measureTone(double hz, std::chrono::milliseconds duration)
{
    // **One frequency at a time, but any number of tones in the signal.**
    // Two applications playing different frequencies into the same capture
    // can each be measured on their own, which is how "summed exactly" is
    // something a test can say.
    constexpr double kRate = 48000.0;
    for (const auto &meter : m_impl->meters) {
        meter->toneCos.store(0.0);
        meter->toneSin.store(0.0);
        meter->toneSamples.store(0);
    }
    m_impl->toneStep.store(2.0 * std::numbers::pi * hz / kRate, std::memory_order_relaxed);
    std::this_thread::sleep_for(duration);
    m_impl->toneStep.store(0.0, std::memory_order_relaxed);

    std::map<std::string, double> result;
    for (const auto &meter : m_impl->meters) {
        const std::uint64_t samples = meter->toneSamples.load();
        if (samples == 0) {
            continue;
        }
        // The amplitude of a sine at that frequency: twice the magnitude of
        // the Fourier coefficient, divided by the number of samples.
        const double real = meter->toneCos.load() / static_cast<double>(samples);
        const double imaginary = meter->toneSin.load() / static_cast<double>(samples);
        const double amplitude = 2.0 * std::sqrt(real * real + imaginary * imaginary);
        result[meter->channel] = amplitude > 0.0 ? 20.0 * std::log10(amplitude) : -200.0;
    }
    return result;
}

std::map<std::string, double>
CaptureNode::measureToneSettled(double hz, std::chrono::milliseconds duration, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::map<std::string, double> previous = measureTone(hz, duration);
    for (;;) {
        std::map<std::string, double> current = measureTone(hz, duration);
        const bool same = !current.empty() && current.size() == previous.size()
            && std::ranges::all_of(current, [&](const auto &entry) {
                   const auto it = previous.find(entry.first);
                   return it != previous.end() && std::abs(it->second - entry.second) < 0.1;
               });
        if (same || std::chrono::steady_clock::now() >= deadline) {
            return current;
        }
        previous = std::move(current);
    }
}

std::uint64_t CaptureNode::cycles() const
{
    return m_impl->cycles.load();
}

const CaptureNodeOptions &CaptureNode::options() const
{
    return m_options;
}

} // namespace rb::pwtest
