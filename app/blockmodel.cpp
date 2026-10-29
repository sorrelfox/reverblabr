// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blockmodel.h"

#include "meters.h"
#include "peakhold.h"
#include "sessionmodel.h"

#include <QQmlEngine>

#include <utility>

using namespace Qt::StringLiterals;

namespace rb::ui
{

BlockModel::BlockModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

BlockModel::~BlockModel() = default;

SessionModel *BlockModel::session() const
{
    return m_session;
}

void BlockModel::setSession(SessionModel *session)
{
    if (m_session == session) {
        return;
    }
    if (m_session) {
        m_session->disconnect(this);
    }
    m_session = session;
    if (m_session) {
        connect(m_session, &SessionModel::changed, this, &BlockModel::refresh);
    }
    Q_EMIT sessionChanged();
    refresh();
}

Meters *BlockModel::meters() const
{
    return m_meters;
}

void BlockModel::setMeters(Meters *meters)
{
    // Asked for by hand or found on our own, either way we have looked.
    m_metersResolved = true;
    if (m_meters == meters) {
        return;
    }
    if (m_meters) {
        disconnect(m_meters, &Meters::watching, this, &BlockModel::applyHolds);
    }
    m_meters = meters;
    if (m_meters) {
        // The hub drops every block's hold when it changes session, so what
        // speaks for the blocks has to speak again afterwards. Which of the
        // two happens first is not something to depend on.
        connect(m_meters, &Meters::watching, this, &BlockModel::applyHolds);
    }
    Q_EMIT metersChanged();
    applyHolds();
}

int BlockModel::count() const
{
    return static_cast<int>(m_rows.size());
}

int BlockModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant BlockModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case BlockIdRole:
        return row.id;
    case TypeIdRole:
        return row.block.value("type"_L1);
    case NicknameRole:
        return row.block.value("nickname"_L1);
    case BlockEnabledRole:
        // A row whose block has gone missing is not a block that is on.
        return row.block.value("enabled"_L1, false);
    case PositionRole:
        return row.block.value("position"_L1, QPointF{});
    case UnknownTypeRole:
        return row.block.value("unknownType"_L1, false);
    default:
        return {};
    }
}

QHash<int, QByteArray> BlockModel::roleNames() const
{
    return {
        {BlockIdRole, QByteArrayLiteral("blockId")},
        {TypeIdRole, QByteArrayLiteral("typeId")},
        {NicknameRole, QByteArrayLiteral("nickname")},
        // **Not "enabled".** A delegate property of that name shadows
        // QQuickItem's own, and a tile whose bypass switched off its mouse
        // handling is a tile nobody can switch back on.
        {BlockEnabledRole, QByteArrayLiteral("blockEnabled")},
        {PositionRole, QByteArrayLiteral("position")},
        {UnknownTypeRole, QByteArrayLiteral("unknownType")},
    };
}

int BlockModel::rowOf(const QString &blockId) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).id == blockId) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

QVariantMap BlockModel::at(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).block : QVariantMap{};
}

void BlockModel::refresh()
{
    // **Before the rows, so that the hub is found once rather than looked
    // for again on every edit.** It cannot be done in the constructor: the
    // QML engine is put on the object after it is built.
    resolveMeters();

    const QStringList ids = m_session ? m_session->blockIds() : QStringList{};
    const int before = count();

    // **Removals first**, from the bottom up, so that the rows still to be
    // looked at keep the numbers they had.
    for (qsizetype row = m_rows.size() - 1; row >= 0; --row) {
        if (ids.contains(m_rows.at(row).id)) {
            continue;
        }
        beginRemoveRows({}, static_cast<int>(row), static_cast<int>(row));
        m_rows.removeAt(row);
        endRemoveRows();
    }

    // Then every row into the place the session holds it in. The session's
    // order is the file's order and the canvas's Tab order, so a block that
    // was put back by an undo has to land where it was, not at the end.
    for (qsizetype target = 0; target < ids.size(); ++target) {
        const QString &id = ids.at(target);
        qsizetype found = -1;
        for (qsizetype row = target; row < m_rows.size(); ++row) {
            if (m_rows.at(row).id == id) {
                found = row;
                break;
            }
        }
        if (found < 0) {
            beginInsertRows({}, static_cast<int>(target), static_cast<int>(target));
            m_rows.insert(target, Row{id, {}});
            endInsertRows();
        } else if (found != target) {
            beginMoveRows({}, static_cast<int>(found), static_cast<int>(found), {}, static_cast<int>(target));
            m_rows.move(found, target);
            endMoveRows();
        }
    }

    // And last what each row says. **Only the rows that really changed**:
    // a moveBlock patch reaches every row here, and telling the canvas that
    // all thirty tiles are new would undo the point of the two loops above.
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        QVariantMap block = m_session ? m_session->block(m_rows.at(row).id) : QVariantMap{};
        if (block == m_rows.at(row).block) {
            continue;
        }
        m_rows[row].block = std::move(block);
        const QModelIndex changed = index(static_cast<int>(row));
        Q_EMIT dataChanged(changed, changed);
    }

    if (count() != before) {
        Q_EMIT countChanged();
    }

    // **After the rows, and on every edit rather than only on the ones that
    // moved a row.** A parameter is not part of what a row holds, so a
    // `peakHold` somebody changed reaches nothing above; here it reaches the
    // hub whatever the edit was.
    applyHolds();
}

void BlockModel::applyHolds()
{
    if (!m_meters || !m_session) {
        return;
    }
    for (const Row &row : std::as_const(m_rows)) {
        if (row.block.value("type"_L1).toString() != kMeterType) {
            continue;
        }
        const QVariant chosen = m_session->param(row.id, u"peakHold"_s);
        // A block written by a later version, or one whose parameter has not
        // been read yet, holds for what the block itself would: a meter with
        // no wait at all is not what an unset setting means.
        const QString choice = chosen.isValid() ? chosen.toString() : peakHold::defaultChoice();
        m_meters->setHold(row.id, peakHold::holdMs(choice));
        m_meters->setDecay(row.id, peakHold::decayDbPerSecond(choice));
    }
}

void BlockModel::resolveMeters()
{
    if (m_metersResolved) {
        return;
    }
    // A model built in C++ with no QML around it — a test — is given its
    // meters by hand or has none. setMeters() records that we have looked.
    QQmlEngine *engine = qmlEngine(this);
    setMeters(engine == nullptr ? nullptr : engine->singletonInstance<Meters *>("io.github.reverblabr", "Meters"));
}

} // namespace rb::ui
