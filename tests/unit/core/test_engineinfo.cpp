// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/engineinfo.h"
#include "pw/version.h"

#include <gtest/gtest.h>

TEST(EngineInfo, ReportsPipeWireVersions)
{
    const rb::core::EngineInfo info = rb::core::engineInfo();
    EXPECT_EQ(info.pipeWireHeaders.toStdString(), rb::pw::headersVersion());
    EXPECT_EQ(info.pipeWireLibrary.toStdString(), rb::pw::libraryVersion());
}
