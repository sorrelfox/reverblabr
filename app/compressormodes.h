// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace rb::ui
{

/// The Simple Compressor's modes, for its face and its panel.
///
/// **What a mode sets comes from the block**, through core::CompressorModes:
/// the panel's read-only view of a mode's settings is a view of the numbers
/// the sound follows, not a second table of them. The words for each mode are
/// the interface's own and live in CompressorWords.qml.
class CompressorModes : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The modes' stable ids, in the order the block offers them.
    Q_PROPERTY(QStringList ids READ ids CONSTANT)
    /// The mode a new block starts in, and the one a session that does not
    /// mention the setting is in.
    Q_PROPERTY(QString defaultId READ defaultId CONSTANT)

public:
    explicit CompressorModes(QObject *parent = nullptr);

    [[nodiscard]] QStringList ids() const;
    [[nodiscard]] QString defaultId() const;

    /// Everything mode \a id sets: threshold, knee and makeup in decibels,
    /// the ratio as n of n:1, attack and release in milliseconds, and the
    /// detector ("peak" or "rms"). Empty for a mode this build does not have.
    [[nodiscard]] Q_INVOKABLE QVariantMap settings(const QString &id) const;
};

} // namespace rb::ui
