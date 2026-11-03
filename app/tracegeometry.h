// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QColor>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rb::ui
{

/// One colour's worth of triangles.
///
/// A colour for each vertex and not a material for each strength, so that a
/// line can fade at its edges where the scene graph has no antialiasing to
/// lend it.
class TraceNode : public QSGGeometryNode
{
public:
    TraceNode()
        : m_geometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0, QSGGeometry::UnsignedIntType)
    {
        m_geometry.setDrawingMode(QSGGeometry::DrawTriangles);
        m_geometry.setVertexDataPattern(QSGGeometry::StreamPattern);
        m_geometry.setIndexDataPattern(QSGGeometry::StreamPattern);
        setGeometry(&m_geometry);
        setMaterial(&m_material);
    }

    void setTriangles(const std::vector<QSGGeometry::ColoredPoint2D> &vertices, const std::vector<quint32> &indices)
    {
        const auto vertexCount = static_cast<int>(vertices.size());
        const auto indexCount = static_cast<int>(indices.size());
        if (m_geometry.vertexCount() != vertexCount || m_geometry.indexCount() != indexCount) {
            m_geometry.allocate(vertexCount, indexCount);
        }
        std::ranges::copy(vertices, m_geometry.vertexDataAsColoredPoint2D());
        std::ranges::copy(indices, m_geometry.indexDataAsUInt());
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry m_geometry;
    QSGVertexColorMaterial m_material;
};

/// Triangles being put together.
class Triangles
{
public:
    void clear()
    {
        m_vertices.clear();
        m_indices.clear();
    }

    void reserve(std::size_t vertices, std::size_t indices)
    {
        m_vertices.reserve(vertices);
        m_indices.reserve(indices);
    }

    void setColor(const QColor &color)
    {
        m_red = color.redF();
        m_green = color.greenF();
        m_blue = color.blueF();
        m_alpha = color.alphaF();
    }

    [[nodiscard]] quint32 vertex(float x, float y, float strength)
    {
        // Premultiplied, as the material wants them.
        const float alpha = m_alpha * strength;
        QSGGeometry::ColoredPoint2D point{};
        point.set(x,
                  y,
                  static_cast<uchar>(std::lround(m_red * alpha * 255.0f)),
                  static_cast<uchar>(std::lround(m_green * alpha * 255.0f)),
                  static_cast<uchar>(std::lround(m_blue * alpha * 255.0f)),
                  static_cast<uchar>(std::lround(alpha * 255.0f)));
        m_vertices.push_back(point);
        return static_cast<quint32>(m_vertices.size() - 1);
    }

    /// One quad from two pairs of vertices already made.
    void quad(quint32 a, quint32 b, quint32 c, quint32 d)
    {
        m_indices.insert(m_indices.end(), {a, b, c, b, d, c});
    }

    void rectangle(float x1, float y1, float x2, float y2, float strength)
    {
        const quint32 a = vertex(x1, y1, strength);
        const quint32 b = vertex(x2, y1, strength);
        const quint32 c = vertex(x1, y2, strength);
        const quint32 d = vertex(x2, y2, strength);
        quad(a, b, c, d);
    }

    [[nodiscard]] const std::vector<QSGGeometry::ColoredPoint2D> &vertices() const
    {
        return m_vertices;
    }
    [[nodiscard]] const std::vector<quint32> &indices() const
    {
        return m_indices;
    }

private:
    std::vector<QSGGeometry::ColoredPoint2D> m_vertices;
    std::vector<quint32> m_indices;
    float m_red = 1.0f;
    float m_green = 1.0f;
    float m_blue = 1.0f;
    float m_alpha = 1.0f;
};

} // namespace rb::ui
