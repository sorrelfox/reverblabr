// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

/// Marks a function as safe to call on the real-time audio thread: it never
/// allocates or frees memory, takes a lock, waits, or makes a system call.
/// Put it after the parameter list, after noexcept, on both the declaration
/// and the definition:
///
///     void process(ProcessContext &context) noexcept RB_NONBLOCKING;
///
/// With Clang it expands to [[clang::nonblocking]]. The compiler then checks
/// annotated functions (-Wfunction-effects), and the rtsan preset stops a
/// test at the first violation at run time. With GCC it expands to nothing;
/// there, the allocation trap in the DSP test kit covers the same ground.
#if defined(__clang__) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define RB_NONBLOCKING [[clang::nonblocking]]
#endif
#endif
#ifndef RB_NONBLOCKING
#define RB_NONBLOCKING
#endif
