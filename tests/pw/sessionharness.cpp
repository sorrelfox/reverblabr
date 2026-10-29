// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sessionharness.h"

namespace rb::pwtest
{

namespace
{

int argc = 1;
char programName[] = "reverblabr-session-test";
char *argv[] = {programName, nullptr};

} // namespace

void SessionTest::SetUp()
{
    PipeWireTest::SetUp();
    m_app = std::make_unique<QCoreApplication>(argc, argv);
}

void SessionTest::TearDown()
{
    m_app.reset();
    PipeWireTest::TearDown();
}

} // namespace rb::pwtest
