// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "virtualdevicefilter.h"

#include "virtualdevices.h"

namespace rb::ui
{

VirtualDeviceFilter::VirtualDeviceFilter(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // **Every change is seen**: a count that only followed resets would
    // miss the device the window has just made.
    connect(this, &QAbstractItemModel::rowsInserted, this, &VirtualDeviceFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &VirtualDeviceFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &VirtualDeviceFilter::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &VirtualDeviceFilter::countChanged);
}

QString VirtualDeviceFilter::direction() const
{
    return m_direction;
}

void VirtualDeviceFilter::setDirection(const QString &direction)
{
    if (direction == m_direction) {
        return;
    }
    m_direction = direction;
    // **invalidate(), not begin/endFilterChange()**, which are newer than
    // the Qt this builds against (6.10); the direction is set once, as the
    // list is made, so filtering everything again costs nothing.
    invalidate();
    Q_EMIT directionChanged();
    Q_EMIT countChanged();
}

int VirtualDeviceFilter::count() const
{
    return rowCount();
}

int VirtualDeviceFilter::rowOf(const QString &deviceId) const
{
    for (int row = 0; row < rowCount(); ++row) {
        if (data(index(row, 0), VirtualDevices::IdRole).toString() == deviceId) {
            return row;
        }
    }
    return -1;
}

bool VirtualDeviceFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex row = sourceModel()->index(sourceRow, 0, sourceParent);
    return sourceModel()->data(row, VirtualDevices::DirectionRole).toString() == m_direction;
}

} // namespace rb::ui
