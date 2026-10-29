// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace rb::core
{

/// **Things a person may want to hear about that are not errors**: a
/// device a running session uses went away, an application it captures
/// stopped playing or started again. Nothing was refused and nothing is
/// wrong with the session; the interface decides whether to say so, and
/// how.
///
/// Like an error, a notice travels as `{code, args}` and holds no words
/// (docs/i18n.md). Every notice names the block it is about in the
/// argument "block", which is not listed below.
enum class NoticeCode {
    DeviceLost,
    DeviceReturned,
    CaptureLost,
    CaptureRegained,
    Count, ///< Not a notice; the number of codes.
};

struct NoticeCodeInfo {
    NoticeCode code;
    std::string_view name;
    std::array<std::string_view, 3> arguments{};
};

inline constexpr std::array kNoticeCodes{
    // The device an Input Device or Output Device block uses left the graph
    // while its session ran: unplugged, switched off, taken by another
    // profile. The block waits for it and carries on when it is back.
    // "device" is its description when it was last seen, else its
    // node.name.
    NoticeCodeInfo{NoticeCode::DeviceLost, "device.lost", {"session", "device"}},
    // That device is back. Sent so that an interface waiting to see whether
    // a loss lasts can let it go; not news in itself.
    NoticeCodeInfo{NoticeCode::DeviceReturned, "device.returned", {"session", "device"}},
    // An Application source was capturing and the application now has no
    // stream that matches: it closed, or stopped playing.
    NoticeCodeInfo{NoticeCode::CaptureLost, "capture.lost", {"session", "application"}},
    // That application is playing again, after a capture.lost.
    NoticeCodeInfo{NoticeCode::CaptureRegained, "capture.regained", {"session", "application"}},
};

static_assert(kNoticeCodes.size() == static_cast<std::size_t>(NoticeCode::Count),
              "every NoticeCode needs exactly one row in kNoticeCodes");
static_assert(
    [] {
        for (std::size_t i = 0; i < kNoticeCodes.size(); ++i) {
            if (kNoticeCodes[i].code != static_cast<NoticeCode>(i)) {
                return false;
            }
        }
        return true;
    }(),
    "kNoticeCodes rows must be in NoticeCode order");

/// The stable name of a notice code, for example "device.lost".
[[nodiscard]] constexpr std::string_view noticeCodeName(NoticeCode code) noexcept
{
    return kNoticeCodes[static_cast<std::size_t>(code)].name;
}

} // namespace rb::core
