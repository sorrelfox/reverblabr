// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// What blocks this build has, as the interface reads them.
///
/// **The parameter descriptors are the settings panel.** A block declares
/// its parameters once and the generic panel is generated from that, so a
/// new block needs no new interface code — which is only true if the
/// interface never keeps a second copy of what a block can be asked.
///
/// **It holds no words.** Everything here is a stable id; the names and
/// descriptions a person reads are the interface's own, translated and
/// keyed by these ids (see docs/i18n.md).
///
/// The catalogue does not change while the engine runs, so it is asked for
/// once and then answered from memory.
class BlockCatalogue : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether the engine has answered yet. A panel opened before it has
    /// shows nothing rather than guessing.
    Q_PROPERTY(bool ready READ isReady NOTIFY changed)
    /// Every type id the build knows, in id order.
    Q_PROPERTY(QStringList types READ types NOTIFY changed)

public:
    explicit BlockCatalogue(QObject *parent = nullptr);
    ~BlockCatalogue() override;

    static BlockCatalogue *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade, asking it for the catalogue at once. A test gives
    /// its own; the QML singleton is given the window's.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] QStringList types() const;

    /// One block type: category, face, ports and params. Empty for a type
    /// this build does not have, which is what a session written by a later
    /// version leads to.
    [[nodiscard]] Q_INVOKABLE QVariantMap describe(const QString &typeId) const;

    /// One block type's parameter descriptors, in the order the block
    /// declares them — which is the order the panel shows them in, because
    /// the block is what knows which of its settings matter most.
    [[nodiscard]] Q_INVOKABLE QVariantList params(const QString &typeId) const;

    /// One parameter's descriptor, or an empty map.
    [[nodiscard]] Q_INVOKABLE QVariantMap describeParam(const QString &typeId, const QString &paramId) const;

    /// Reads a catalogue that did not come from the engine. **For tests**,
    /// so that a panel can be checked against a block that does not exist.
    void setCatalogue(const QJsonObject &catalogue);

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
