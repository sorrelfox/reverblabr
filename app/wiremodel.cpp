// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "wiremodel.h"

#include "sessionmodel.h"

#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::ui
{

bool WireModel::Row::joins(const QString &from, const QString &to, int lane) const
{
    return from == fromBlock && to == toBlock && lane == toLane;
}

WireModel::WireModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

WireModel::~WireModel() = default;

SessionModel *WireModel::session() const
{
    return m_session;
}

void WireModel::setSession(SessionModel *session)
{
    if (m_session == session) {
        return;
    }
    if (m_session) {
        m_session->disconnect(this);
    }
    m_session = session;
    if (m_session) {
        connect(m_session, &SessionModel::changed, this, &WireModel::refresh);
    }
    Q_EMIT sessionChanged();
    refresh();
}

int WireModel::count() const
{
    return static_cast<int>(m_rows.size());
}

QVariantMap WireModel::inputLaneWidths() const
{
    return m_inputLaneWidths;
}

void WireModel::setInputLaneWidths(const QVariantMap &widths)
{
    if (m_inputLaneWidths == widths) {
        return;
    }
    m_inputLaneWidths = widths;
    Q_EMIT inputLaneWidthsChanged();
    refresh();
}

int WireModel::laneWidthOf(const QString &blockId) const
{
    if (m_inputLaneWidths.isEmpty() || !m_session) {
        return 0;
    }
    return m_inputLaneWidths.value(m_session->blockType(blockId)).toInt();
}

int WireModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant WireModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case FromBlockRole:
        return row.fromBlock;
    case ToBlockRole:
        return row.toBlock;
    case ChannelsRole:
        return row.channels;
    case MergingRole:
        return row.merging;
    case ToLaneRole:
        return row.toLane;
    default:
        return {};
    }
}

QHash<int, QByteArray> WireModel::roleNames() const
{
    return {
        {FromBlockRole, QByteArrayLiteral("fromBlock")},
        {ToBlockRole, QByteArrayLiteral("toBlock")},
        {ChannelsRole, QByteArrayLiteral("channels")},
        {MergingRole, QByteArrayLiteral("merging")},
        {ToLaneRole, QByteArrayLiteral("toLane")},
    };
}

QVariantMap WireModel::at(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    const Row &link = m_rows.at(row);
    return QVariantMap{
        {"fromBlock"_L1, link.fromBlock},
        {"toBlock"_L1, link.toBlock},
        {"toLane"_L1, link.toLane},
        {"channels"_L1, link.channels},
        {"merging"_L1, link.merging},
    };
}

void WireModel::refresh()
{
    // **The links, in the order the session's wires first name them.** A
    // link is a pair of blocks and the channels between them; the session
    // holds the channels, and grouping them here is what lets the canvas
    // draw one curve where a person drew one link.
    QList<Row> wanted;
    const QVariantList wires = m_session ? m_session->wires() : QVariantList{};
    for (const QVariant &entry : wires) {
        const QVariantMap wire = entry.toMap();
        const QString from = wire.value("fromBlock"_L1).toString();
        const QString to = wire.value("toBlock"_L1).toString();
        const int width = laneWidthOf(to);
        const int lane = width > 0 ? wire.value("toPort"_L1).toInt() / width : 0;
        const auto found =
            std::ranges::find_if(wanted, [&from, &to, lane](const Row &row) { return row.joins(from, to, lane); });
        if (found == wanted.end()) {
            wanted.append(Row{from, to, lane, 1, false});
            continue;
        }
        ++found->channels;
    }

    const int before = count();

    // **Removals first**, from the bottom up, so that the rows still to be
    // looked at keep the numbers they had.
    for (qsizetype row = m_rows.size() - 1; row >= 0; --row) {
        const Row &existing = m_rows.at(row);
        const bool kept = std::ranges::any_of(wanted, [&existing](const Row &row) {
            return row.joins(existing.fromBlock, existing.toBlock, existing.toLane);
        });
        if (kept) {
            continue;
        }
        beginRemoveRows({}, static_cast<int>(row), static_cast<int>(row));
        m_rows.removeAt(row);
        endRemoveRows();
    }

    // Then every link into the place the session holds it in, so that one
    // put back by an undo lands where it was rather than at the end.
    for (qsizetype target = 0; target < wanted.size(); ++target) {
        const Row &link = wanted.at(target);
        qsizetype found = -1;
        for (qsizetype row = target; row < m_rows.size(); ++row) {
            if (m_rows.at(row).joins(link.fromBlock, link.toBlock, link.toLane)) {
                found = row;
                break;
            }
        }
        if (found < 0) {
            beginInsertRows({}, static_cast<int>(target), static_cast<int>(target));
            m_rows.insert(target, Row{link.fromBlock, link.toBlock, link.toLane, link.channels, false});
            endInsertRows();
        } else if (found != target) {
            beginMoveRows({}, static_cast<int>(found), static_cast<int>(found), {}, static_cast<int>(target));
            m_rows.move(found, target);
            endMoveRows();
        }
    }

    // And last the two things about a row that can change while the row
    // stays what it is: how many channels it carries, and whether the lane
    // it arrives at is summing. **Only the rows that really changed**,
    // because a tile being dragged reaches every row here.
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        const Row &link = m_rows.at(row);
        const int channels = wanted.at(row).channels;
        const int width = laneWidthOf(link.toBlock);
        const bool merging = m_session
            && (width > 0 ? m_session->arrivingLinks(link.toBlock, link.toLane * width, width)
                          : m_session->arrivingLinks(link.toBlock))
                > 1;
        QList<int> roles;
        if (channels != link.channels) {
            m_rows[row].channels = channels;
            roles.append(ChannelsRole);
        }
        if (merging != link.merging) {
            m_rows[row].merging = merging;
            roles.append(MergingRole);
        }
        if (roles.isEmpty()) {
            continue;
        }
        const QModelIndex changed = index(static_cast<int>(row));
        Q_EMIT dataChanged(changed, changed, roles);
    }

    if (count() != before) {
        Q_EMIT countChanged();
    }
}

} // namespace rb::ui
