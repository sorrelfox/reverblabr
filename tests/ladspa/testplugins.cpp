// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// LADSPA plugins for the tests, built here so that hosting LADSPA is
// tested on every machine, with nothing installed:
//
// - rb_stereo_gain: two channels in and out, times "Gain" (0 to 4, 1).
// - rb_mono_gain: the same, one channel.
// - rb_hints: one channel through, with a control for every kind of range
//   hint, two controls of one name, and a "latency" output saying 64. It
//   says it cannot write its output over its input, and it cannot: it
//   clears its output before it reads.

#include "plugin/ladspaabi.h"

#include <algorithm>
#include <array>
#include <new>

namespace
{

using namespace rb::ladspa;

struct Instance {
    std::array<Data *, 16> ports{};
};

Handle instantiate(const Descriptor *, unsigned long)
{
    return new (std::nothrow) Instance{};
}

void connect(Handle handle, unsigned long port, Data *data)
{
    auto *instance = static_cast<Instance *>(handle);
    if (port < instance->ports.size()) {
        instance->ports[port] = data;
    }
}

void cleanup(Handle handle)
{
    delete static_cast<Instance *>(handle);
}

// ------------------------------------------------------------- the gains

// Stereo: 0 in L, 1 in R, 2 out L, 3 out R, 4 gain. Mono: 0 in, 1 out,
// 2 gain.
constexpr std::array<int, 5> kStereoPorts{port::kAudio | port::kInput,
                                          port::kAudio | port::kInput,
                                          port::kAudio | port::kOutput,
                                          port::kAudio | port::kOutput,
                                          port::kControl | port::kInput};
constexpr std::array<const char *, 5> kStereoNames{"Input L", "Input R", "Output L", "Output R", "Gain"};
constexpr PortRangeHint kGainHint{hint::kBoundedBelow | hint::kBoundedAbove | hint::kDefault1, 0.0f, 4.0f};
constexpr std::array<PortRangeHint, 5> kStereoHints{PortRangeHint{0, 0, 0},
                                                    PortRangeHint{0, 0, 0},
                                                    PortRangeHint{0, 0, 0},
                                                    PortRangeHint{0, 0, 0},
                                                    kGainHint};

void runStereo(Handle handle, unsigned long frames)
{
    const auto &p = static_cast<Instance *>(handle)->ports;
    const Data gain = *p[4];
    for (unsigned long i = 0; i < frames; ++i) {
        p[2][i] = p[0][i] * gain;
        p[3][i] = p[1][i] * gain;
    }
}

constexpr std::array<int, 3> kMonoPorts{port::kAudio | port::kInput,
                                        port::kAudio | port::kOutput,
                                        port::kControl | port::kInput};
constexpr std::array<const char *, 3> kMonoNames{"Input", "Output", "Gain"};
constexpr std::array<PortRangeHint, 3> kMonoHints{PortRangeHint{0, 0, 0}, PortRangeHint{0, 0, 0}, kGainHint};

void runMono(Handle handle, unsigned long frames)
{
    const auto &p = static_cast<Instance *>(handle)->ports;
    const Data gain = *p[2];
    for (unsigned long i = 0; i < frames; ++i) {
        p[1][i] = p[0][i] * gain;
    }
}

// ------------------------------------------------------------- the hints

// 0 in, 1 out, then the controls, then the latency.
constexpr std::array<int, 10> kHintPorts{port::kAudio | port::kInput,
                                         port::kAudio | port::kOutput,
                                         port::kControl | port::kInput,
                                         port::kControl | port::kInput,
                                         port::kControl | port::kInput,
                                         port::kControl | port::kInput,
                                         port::kControl | port::kInput,
                                         port::kControl | port::kInput,
                                         port::kControl | port::kInput,
                                         port::kControl | port::kOutput};
constexpr std::array<const char *, 10>
    kHintNames{"Input", "Output", "Frequency (Hz)", "Steps", "Bypass", "Level", "Gain", "Gain", "Unbounded", "latency"};
constexpr std::array<PortRangeHint, 10> kHintHints{
    PortRangeHint{0, 0, 0},
    PortRangeHint{0, 0, 0},
    // A fraction of the rate, on a logarithmic scale, 440 by default.
    PortRangeHint{hint::kBoundedBelow | hint::kBoundedAbove | hint::kSampleRate | hint::kLogarithmic
                      | hint::kDefault440,
                  0.0001f,
                  0.5f},
    // Whole numbers, from 1 to 8, the middle by default.
    PortRangeHint{hint::kBoundedBelow | hint::kBoundedAbove | hint::kInteger | hint::kDefaultMiddle, 1.0f, 8.0f},
    PortRangeHint{hint::kToggled | hint::kDefault0, 0.0f, 0.0f},
    // A quarter of the way along a logarithmic scale, 0.1.
    PortRangeHint{hint::kBoundedBelow | hint::kBoundedAbove | hint::kLogarithmic | hint::kDefaultLow, 0.01f, 100.0f},
    PortRangeHint{hint::kBoundedBelow | hint::kBoundedAbove | hint::kDefaultMaximum, -1.0f, 2.0f},
    PortRangeHint{hint::kBoundedBelow | hint::kBoundedAbove | hint::kDefaultMinimum, -1.0f, 2.0f},
    PortRangeHint{0, 0.0f, 0.0f},
    PortRangeHint{0, 0.0f, 0.0f},
};
constexpr int kInPlaceBroken = 0x2;
constexpr Data kHintLatency = 64.0f;

void runHints(Handle handle, unsigned long frames)
{
    const auto &p = static_cast<Instance *>(handle)->ports;
    // **Cannot write over its input**: were the two one buffer, clearing
    // the output would clear the input before it is read.
    std::fill_n(p[1], frames, 0.0f);
    for (unsigned long i = 0; i < frames; ++i) {
        p[1][i] += p[0][i];
    }
    *p[9] = kHintLatency;
}

// ------------------------------------------------------------ descriptors

Descriptor describe(unsigned long id,
                    const char *label,
                    const char *name,
                    int properties,
                    unsigned long count,
                    const int *ports,
                    const char *const *names,
                    const PortRangeHint *hints,
                    void (*run)(Handle, unsigned long))
{
    return Descriptor{id,
                      label,
                      properties,
                      name,
                      "Reverblabr",
                      "MPL-2.0",
                      count,
                      ports,
                      names,
                      hints,
                      nullptr,
                      instantiate,
                      connect,
                      nullptr,
                      run,
                      nullptr,
                      nullptr,
                      nullptr,
                      cleanup};
}

const std::array<Descriptor, 3> kDescriptors{
    describe(4990001,
             "rb_stereo_gain",
             "Test Stereo Gain",
             0,
             kStereoPorts.size(),
             kStereoPorts.data(),
             kStereoNames.data(),
             kStereoHints.data(),
             runStereo),
    describe(4990002,
             "rb_mono_gain",
             "Test Mono Gain",
             0,
             kMonoPorts.size(),
             kMonoPorts.data(),
             kMonoNames.data(),
             kMonoHints.data(),
             runMono),
    describe(4990003,
             "rb_hints",
             "Test Hints",
             kInPlaceBroken,
             kHintPorts.size(),
             kHintPorts.data(),
             kHintNames.data(),
             kHintHints.data(),
             runHints),
};

} // namespace

extern "C" __attribute__((visibility("default"))) const Descriptor *ladspa_descriptor(unsigned long index)
{
    return index < kDescriptors.size() ? &kDescriptors[index] : nullptr;
}
