// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/logger.h"

#include <cstdio>

namespace rb::rt
{

const char *logLevelName(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    }
    return "unknown";
}

std::size_t RealtimeLogger::drainToStandardError()
{
    return drain([](const LogRecord &record) {
        // Log messages are English and never reach the interface, so there is
        // nothing here to translate; see docs/i18n.md.
        switch (record.argumentCount) {
        case 0:
            std::fprintf(stderr,
                         "reverblabr[rt] %llu %s: %s\n",
                         static_cast<unsigned long long>(record.sequence),
                         logLevelName(record.level),
                         record.message);
            break;
        case 1:
            std::fprintf(stderr,
                         "reverblabr[rt] %llu %s: %s %lld\n",
                         static_cast<unsigned long long>(record.sequence),
                         logLevelName(record.level),
                         record.message,
                         static_cast<long long>(record.first));
            break;
        default:
            std::fprintf(stderr,
                         "reverblabr[rt] %llu %s: %s %lld %lld\n",
                         static_cast<unsigned long long>(record.sequence),
                         logLevelName(record.level),
                         record.message,
                         static_cast<long long>(record.first),
                         static_cast<long long>(record.second));
            break;
        }
    });
}

} // namespace rb::rt
