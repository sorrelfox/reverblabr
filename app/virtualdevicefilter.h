// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QString>

namespace rb::ui
{

/// One direction's virtual devices, out of the VirtualDevices list:
/// the outputs apps play into, or the microphones apps record from.
///
/// **Two lists rather than one list in sections.** The engine lists the
/// user's own devices first and the all-users ones after, with the two
/// directions mixed, and a section header only appears for a section that
/// has something in it. The window shows both kinds with what each is for
/// even while one of them is empty, so each gets a list of its own.
///
/// **A filter, not a copy**: rows stay the model's own, in the model's
/// order, so a row that changes changes here in place.
class VirtualDeviceFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    /// "output" or "input": which devices this list shows.
    Q_PROPERTY(QString direction READ direction WRITE setDirection NOTIFY directionChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit VirtualDeviceFilter(QObject *parent = nullptr);

    [[nodiscard]] QString direction() const;
    void setDirection(const QString &direction);
    [[nodiscard]] int count() const;

    /// The row the device \a deviceId is in here, or -1 when it is not one
    /// of this list's.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &deviceId) const;

Q_SIGNALS:
    void directionChanged();
    void countChanged();

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_direction;
};

} // namespace rb::ui
