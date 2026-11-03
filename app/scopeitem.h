// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "scopesurface.h"

#include <QColor>
#include <QString>
#include <QVariantList>

namespace rb::ui
{

/// **The sound at a point in a lane, drawn on the scene graph.**
///
/// Scene-graph geometry and not Canvas, for the reason MeterItem gives, and
/// more so: this redraws a whole trace, maybe at every frame.
class ScopeItem : public ScopeSurface
{
    Q_OBJECT
    QML_ELEMENT

    /// The block's settings, as they are spelled in the session.
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal spanMs READ spanMs WRITE setSpanMs NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal gainDb READ gainDb WRITE setGainDb NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool autoFit READ autoFit WRITE setAutoFit NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString channels READ channels WRITE setChannels NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool trigger READ trigger WRITE setTrigger NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString heightScale READ heightScale WRITE setHeightScale NOTIFY settingsChanged FINAL)

    /// Left, or the one channel shown; and right.
    Q_PROPERTY(QColor firstColor READ firstColor WRITE setFirstColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor secondColor READ secondColor WRITE setSecondColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged FINAL)

    /// With the trigger on: whether the last trace started on a crossing.
    Q_PROPERTY(bool crossed READ crossed NOTIFY readingsChanged FINAL)

public:
    /// A bar's pitch and a dot's, in columns.
    static constexpr int kBarPitch = 4;
    static constexpr int kDotPitch = 3;
    /// The most columns drawn, whatever the width.
    static constexpr int kMaxColumns = 4096;

    explicit ScopeItem(QQuickItem *parent = nullptr);
    ~ScopeItem() override;

    [[nodiscard]] QString style() const;
    void setStyle(const QString &style);
    [[nodiscard]] qreal spanMs() const;
    void setSpanMs(qreal ms);
    [[nodiscard]] qreal gainDb() const;
    void setGainDb(qreal db);
    [[nodiscard]] bool autoFit() const;
    void setAutoFit(bool on);
    [[nodiscard]] QString channels() const;
    void setChannels(const QString &channels);
    [[nodiscard]] bool trigger() const;
    void setTrigger(bool on);
    [[nodiscard]] bool grid() const;
    void setGrid(bool on);
    [[nodiscard]] QString heightScale() const;
    void setHeightScale(const QString &scale);

    [[nodiscard]] QColor firstColor() const;
    void setFirstColor(const QColor &color);
    [[nodiscard]] QColor secondColor() const;
    void setSecondColor(const QColor &color);
    [[nodiscard]] QColor gridColor() const;
    void setGridColor(const QColor &color);

    [[nodiscard]] bool crossed() const;

    /// The heights of one trace now, for a test: [low, high, low, high, ...].
    [[nodiscard]] Q_INVOKABLE QVariantList heights(int trace) const;

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void startAfresh() override;
    bool workOutPicture() override;
    void sayOwnReadings() override;

private:
    [[nodiscard]] int columnCount() const;

    QString m_style{QStringLiteral("line")};
    QString m_channels{QStringLiteral("both")};
    QString m_scale{QStringLiteral("linear")};
    bool m_grid = true;

    QColor m_firstColor{QStringLiteral("#3daee9")};
    QColor m_secondColor{QStringLiteral("#f5a623")};
    QColor m_gridColor{QStringLiteral("#31363b")};

    ScopeView m_view;
    bool m_saidCrossed = false;
};

} // namespace rb::ui
