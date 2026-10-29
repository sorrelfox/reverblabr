// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/engineinfo.h"

#include "pw/version.h"

namespace rb::core
{

namespace
{
QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
} // namespace

EngineInfo engineInfo()
{
    return EngineInfo{
        .pipeWireHeaders = toQString(pw::headersVersion()),
        .pipeWireLibrary = toQString(pw::libraryVersion()),
    };
}

} // namespace rb::core
