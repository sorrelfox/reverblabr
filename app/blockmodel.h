// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// The whole types, not forward declarations: the properties are pointers to
// them and moc registers the metatypes, which needs the definitions.
#include "meters.h"
#include "sessionmodel.h"

#include <QAbstractListModel>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

namespace rb::ui
{

/// The session's blocks as rows, for the canvas to lay tiles out from.
///
/// **A list model rather than a Repeater over blockIds** ([ADR-0011]). The
/// session tells the window what changed by saying that something did; a
/// canvas that re-read a string list from that would throw every tile away
/// and build it again, losing the selection, the focus ring and any drag
/// under way, and taking the faces and their meters with it. This works out
/// what really changed and says so row by row, so a block that did not move
/// keeps its delegate.
///
/// **It owns nothing.** The session model is still the one copy of what the
/// session holds; this is a view of it, and every edit goes back through the
/// session model to the engine.
///
/// **It also speaks to the meter hub for every meter block**. A
/// Peak/RMS block's `peakHold` is a parameter of the block, but the waiting
/// is done in the hub, so somebody has to carry the one to the other. Until
/// now the only thing that did was the block's settings panel, which meant a
/// meter held for its own setting exactly when somebody had opened its panel
/// — and for the hub's default otherwise, in a session where the parameter
/// was right there. This is what already follows every block in the session,
/// whether it has a tile on the screen or a panel open or neither, so it is
/// where "every meter block's hold reaches the hub" can be true rather than
/// likely.
class BlockModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    /// The session to follow. Null leaves the model empty, which is what a
    /// window with no session open shows.
    Q_PROPERTY(rb::ui::SessionModel *session READ session WRITE setSession NOTIFY sessionChanged)
    /// How many blocks there are, for an empty-state hint that would
    /// otherwise have to count rows itself.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Where every meter block's hold is pushed.
    ///
    /// **The window's own singleton unless it is set**, because there is one
    /// meter hub per window and a model that had to be wired up by hand is a
    /// model somebody forgets to wire up — which is how a meter's hold once
    /// stopped reaching the hub. A test sets one of its own.
    Q_PROPERTY(rb::ui::Meters *meters READ meters WRITE setMeters NOTIFY metersChanged)

public:
    enum Role : int {
        BlockIdRole = Qt::UserRole + 1, ///< Unique within the session, for example "b1".
        TypeIdRole, ///< The block type, for example "effect.volume".
        NicknameRole, ///< What the user called it, or empty for the type's own name.
        BlockEnabledRole, ///< False means bypassed, which the graph cross-fades.
        PositionRole, ///< Where the tile sits on the canvas, as a QPointF.
        UnknownTypeRole, ///< This build has no such block; the session was written by another.
    };
    Q_ENUM(Role)

    explicit BlockModel(QObject *parent = nullptr);
    ~BlockModel() override;

    /// The block type whose `peakHold` the hub is told about. Spelled out
    /// here rather than included from the DSP layer, which the interface
    /// does not depend on. Were it wrong, no meter block would ever be told
    /// anything, which is exactly what the tests below would notice.
    static constexpr QLatin1StringView kMeterType{"meter.peakrms"};

    [[nodiscard]] SessionModel *session() const;
    void setSession(SessionModel *session);

    [[nodiscard]] Meters *meters() const;
    void setMeters(Meters *meters);

    [[nodiscard]] int count() const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Which row a block is in, or -1. **What lets the canvas answer a
    /// question about a block id** — the selection is ids, because a row
    /// number stops meaning the same block the moment one is removed.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &blockId) const;

    /// One row, as SessionModel::block() spells it. **For the canvas's own
    /// arithmetic** — fitting the view, deciding what a rubber band caught
    /// — which has to read every block whether its tile has been built yet
    /// or not. An empty map for a row that is not there.
    [[nodiscard]] Q_INVOKABLE QVariantMap at(int row) const;

Q_SIGNALS:
    void sessionChanged();
    void countChanged();
    void metersChanged();

private:
    /// Brings the rows back in step with the session, row by row.
    void refresh();

    /// Tells the hub what every meter block in the session holds for.
    ///
    /// **All of them every time, rather than the ones that changed.** It is
    /// a hash insert per meter block, against a session edit rather than a
    /// frame; working out which holds had moved would cost more than saying
    /// all of them, and would be a second thing to get wrong.
    void applyHolds();

    /// Finds the window's meters the first time they are wanted. The QML
    /// engine is not there yet while the object is being constructed, so
    /// this cannot be done in the constructor.
    void resolveMeters();

    struct Row {
        QString id;
        /// The whole of SessionModel::block(), so that "did this row change"
        /// is one comparison rather than one per role.
        QVariantMap block;
    };

    QPointer<SessionModel> m_session;
    QPointer<Meters> m_meters;
    /// Whether the window's singleton has been looked for. Separate from
    /// the pointer, so that a window with no QML engine — a unit test — is
    /// not asked again on every edit.
    bool m_metersResolved = false;
    QList<Row> m_rows;
};

} // namespace rb::ui
