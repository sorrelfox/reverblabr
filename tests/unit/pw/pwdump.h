// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "pw/pwobjects.h"

#include <QList>
#include <QString>

namespace rb::test
{

/// A graph as `pw-dump` writes it out, read into the objects the mirror
/// holds.
struct PwDump {
    QList<pw::PwNode> nodes;
    QList<pw::PwPort> ports;
    QList<pw::PwLink> links;
    QList<pw::PwDevice> devices;
};

/// Reads the `pw-dump` file at \a path.
///
/// **Each object's properties go through the mirror's own readers**
/// (pw::readNodeProperties() and the rest), turned into text first as
/// PipeWire hands them over: a graph written out by hand then says what a
/// live one would. What pw-dump keeps outside the properties, a node's
/// state and a link's ends, is read from its info. Anything else in the
/// file, a client, a module, a metadata object, is left out.
///
/// An unreadable file is an empty graph, which the test that loaded it
/// will notice.
[[nodiscard]] PwDump readPwDump(const QString &path);

} // namespace rb::test
