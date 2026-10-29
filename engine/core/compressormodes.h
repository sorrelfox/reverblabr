// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace rb::core
{

/// The Simple Compressor's modes, for the interface to show.
///
/// **The panel shows what a mode sets, and may not have a copy of it.** Each
/// mode is a row of numbers in the block, and the block is what the sound
/// follows; a second table in the interface would be a description that
/// slowly stopped being true. The interface layer cannot reach the block
/// layer (docs/architecture.md), so the one table is handed up through here,
/// as core::DynamicsCurve hands up the Dynamics block's curve.
class CompressorModes
{
public:
    /// The type id of the block these modes belong to.
    [[nodiscard]] static QString typeId();

    /// The modes' stable ids, in the order the block offers them.
    [[nodiscard]] static QStringList ids();

    /// The mode a new block starts in.
    [[nodiscard]] static QString defaultId();

    /// Everything mode \a id sets, by name: "threshold", "knee" and
    /// "makeup" in decibels, "ratio" as the first number of n:1, "attack"
    /// and "release" in milliseconds, and "detector", which is "peak" or
    /// "rms". Empty for a mode this build does not have.
    [[nodiscard]] static QVariantMap settings(const QString &id);
};

} // namespace rb::core
