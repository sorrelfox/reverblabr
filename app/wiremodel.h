// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// The whole type, not a forward declaration: the property is a pointer to
// it and moc registers the metatype, which needs the definition.
#include "sessionmodel.h"

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QQmlEngine>
#include <QString>

namespace rb::ui
{

/// The session's links as rows, for the canvas to draw curves from.
///
/// **A row is a link, not a wire**. A link carries a stereo signal
/// and the session holds it as a wire a channel, because a PipeWire link
/// only ever joins two ports of the same channel position ([ADR-0007]).
/// What a person drew, though, is one thing joining two blocks, and one
/// thing is what the canvas draws: two curves lying on top of each other,
/// one per channel, only look like a thicker line that is twice as
/// expensive. So the wires are grouped here, where the session is already
/// being turned into rows, and the canvas is spared knowing that a link
/// has channels at all.
///
/// **A list model for the same reason the blocks have one** ([ADR-0011] and
/// BlockModel): the session says only that something changed, and a
/// Repeater over a list read again from that would throw every link away
/// and build it again — losing which one was selected and restarting the
/// flow animation on all of them, sixty times a second while a tile is
/// dragged. This works out what really changed and says so row by row.
///
/// **A link has no id, because a link is its two ends.** A row is
/// identified by the two blocks it joins, and the lane it arrives at on a
/// block with more than one, and nothing else, so a row never
/// changes what it is: a link that moved is a link removed and a link
/// added. What can change under a row that stays is how many channels it
/// carries, and whether the lane it arrives at sums.
///
/// **It owns nothing.** Every edit goes back through the session model.
class WireModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    /// The session to follow. Null leaves the model empty.
    Q_PROPERTY(rb::ui::SessionModel *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// How many ports make one input lane, by block type, for the types
    /// with more than one: `{"advanced.ducking": 2}`. A type it does
    /// not name has one lane on its input side. **Handed down by the
    /// canvas**, which reads it from the catalogue; this model knows only
    /// the session.
    Q_PROPERTY(QVariantMap inputLaneWidths READ inputLaneWidths WRITE setInputLaneWidths NOTIFY inputLaneWidthsChanged)

public:
    enum Role : int {
        FromBlockRole = Qt::UserRole + 1, ///< The block the signal leaves.
        ToBlockRole, ///< The block the signal arrives at.
        ChannelsRole, ///< How many channels the link carries.
        MergingRole, ///< More than one link arrives at that lane: the engine sums them.
        ToLaneRole, ///< Which of the arriving block's input lanes the link arrives at.
    };
    Q_ENUM(Role)

    explicit WireModel(QObject *parent = nullptr);
    ~WireModel() override;

    [[nodiscard]] SessionModel *session() const;
    void setSession(SessionModel *session);

    [[nodiscard]] int count() const;

    [[nodiscard]] QVariantMap inputLaneWidths() const;
    void setInputLaneWidths(const QVariantMap &widths);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// One row, as `{fromBlock, toBlock, toLane, channels, merging}`, for the
    /// canvas's own arithmetic — which link a click landed on, what a tile
    /// is joined to — and for the rows whose curve has not been built yet.
    [[nodiscard]] Q_INVOKABLE QVariantMap at(int row) const;

Q_SIGNALS:
    void sessionChanged();
    void countChanged();
    void inputLaneWidthsChanged();

private:
    /// Brings the rows back in step with the session, row by row.
    void refresh();

    /// How many ports one input lane of \a blockId is, or nought for a
    /// block with one lane.
    [[nodiscard]] int laneWidthOf(const QString &blockId) const;

    struct Row {
        QString fromBlock;
        QString toBlock;
        int toLane = 0;
        int channels = 0;
        bool merging = false;

        /// The two ends, and the lane at the far one: what makes this row
        /// the link it is. A source wired to both of a ducker's lanes is two
        /// links.
        [[nodiscard]] bool joins(const QString &from, const QString &to, int lane) const;
    };

    QPointer<SessionModel> m_session;
    QVariantMap m_inputLaneWidths;
    QList<Row> m_rows;
};

} // namespace rb::ui
