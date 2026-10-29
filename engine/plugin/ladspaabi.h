// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// **The LADSPA 1.1 binary interface**, declared here rather than taken
// from an SDK header, so that hosting LADSPA needs nothing installed to build:
// a LADSPA library is found with dlopen() and speaks only through the
// structures below. The layout is the one every LADSPA library is built
// against and must not change; the names follow the specification's.

namespace rb::ladspa
{

using Data = float;
using Handle = void *;

/// What a port is: LADSPA_PortDescriptor's bits.
namespace port
{
inline constexpr int kInput = 0x1;
inline constexpr int kOutput = 0x2;
inline constexpr int kControl = 0x4;
inline constexpr int kAudio = 0x8;
} // namespace port

/// What a control port's range says: LADSPA_PortRangeHintDescriptor's bits.
namespace hint
{
inline constexpr int kBoundedBelow = 0x1;
inline constexpr int kBoundedAbove = 0x2;
inline constexpr int kToggled = 0x4;
/// The bounds are fractions of the sample rate.
inline constexpr int kSampleRate = 0x8;
inline constexpr int kLogarithmic = 0x10;
inline constexpr int kInteger = 0x20;
inline constexpr int kDefaultMask = 0x3C0;
inline constexpr int kDefaultNone = 0x0;
inline constexpr int kDefaultMinimum = 0x40;
inline constexpr int kDefaultLow = 0x80;
inline constexpr int kDefaultMiddle = 0xC0;
inline constexpr int kDefaultHigh = 0x100;
inline constexpr int kDefaultMaximum = 0x140;
inline constexpr int kDefault0 = 0x200;
inline constexpr int kDefault1 = 0x240;
inline constexpr int kDefault100 = 0x280;
inline constexpr int kDefault440 = 0x2C0;
} // namespace hint

struct PortRangeHint {
    int HintDescriptor;
    Data LowerBound;
    Data UpperBound;
};

struct Descriptor {
    unsigned long UniqueID;
    const char *Label;
    int Properties;
    const char *Name;
    const char *Maker;
    const char *Copyright;
    unsigned long PortCount;
    const int *PortDescriptors;
    const char *const *PortNames;
    const PortRangeHint *PortRangeHints;
    void *ImplementationData;
    Handle (*instantiate)(const Descriptor *descriptor, unsigned long sampleRate);
    void (*connect_port)(Handle instance, unsigned long port, Data *data);
    void (*activate)(Handle instance);
    void (*run)(Handle instance, unsigned long sampleCount);
    void (*run_adding)(Handle instance, unsigned long sampleCount);
    void (*set_run_adding_gain)(Handle instance, Data gain);
    void (*deactivate)(Handle instance);
    void (*cleanup)(Handle instance);
};

/// The one function a LADSPA library exports: its plugins by index, then
/// null.
using DescriptorFunction = const Descriptor *(*)(unsigned long index);
inline constexpr const char *kDescriptorSymbol = "ladspa_descriptor";

} // namespace rb::ladspa
