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
#include <QUrl>
#include <QVariantMap>

namespace rb::ui
{

/// The words and the picture that go with a block type.
///
/// **This is the other half of the catalogue.** BlockCatalogue holds what a
/// block can be asked and nothing a person reads; this holds everything a
/// person reads and nothing about how a block works. The engine can
/// therefore go on knowing no language at all, and a name can be changed, or
/// translated, without touching the audio code (see docs/i18n.md).
///
/// **Every block, not only the ones this build has.** The library sidebar
/// and the canvas are given type ids, and a type id with no name is a blank
/// row; a session written by a later version, or a build without a plugin
/// host, would produce exactly that. So the table covers the whole planned
/// catalogue, and the ids of the blocks that have not been written yet are
/// the ids those blocks will register under. A test checks the other
/// direction — that nothing the registry knows is missing here — which is
/// what catches a block that lands under a different id.
///
/// **The tint is not here.** A category's colour has to be read off the
/// window's colour scheme, so it lives in the interface's own palette
/// (AppColors.categoryTint) and is looked up with the category this table
/// gives. A colour written down in C++ would be a colour that is wrong on
/// every desktop but one.
namespace blockPresentation
{

/// Every type id the table has, in the order the library reads: by category,
/// and inside a category by name.
[[nodiscard]] QStringList types();

/// The categories, by the engine's stable names, in the order the library
/// shows them. **An order, not an alphabet**: a session is built along the
/// signal — where it comes from, what happens to it, where it goes — and
/// sorting the headings by name would put Advanced at the top.
[[nodiscard]] QStringList categories();

/// What a category is called. Empty for a name this table has never heard
/// of, which is what a session written by a later version leads to.
[[nodiscard]] QString categoryName(const QString &category);

/// Whether the table has an entry for \a typeId.
[[nodiscard]] bool has(const QString &typeId);

/// What a connector says it is for, on a block that takes more than one
/// signal: "Main" for "main", "Background" for "background". The
/// lane's own id for one the table does not have.
[[nodiscard]] QString laneName(const QString &laneId);

/// The block's name, as it appears in the library and on its tile. An
/// unknown type is its own name: a session from a later version still shows
/// something a person can search for and tell us about, which an empty row
/// does not.
[[nodiscard]] QString name(const QString &typeId);

/// One line saying what the block is for, short enough for a library row.
/// Empty for an unknown type, because a guess would be worse than nothing.
[[nodiscard]] QString description(const QString &typeId);

/// The longer form, for the library's tooltip. At most two sentences.
[[nodiscard]] QString tooltip(const QString &typeId);

/// The category the block belongs to, by the engine's stable name
/// ("sources", "effects", …), which is also what the tint follows.
[[nodiscard]] QString category(const QString &typeId);

/// Where the block's icon is. Ours, drawn as a single-colour mask, so the
/// theme and the category tint colour it rather than the file doing it.
[[nodiscard]] QUrl icon(const QString &typeId);

} // namespace blockPresentation

/// The same, for QML: the library sidebar, the canvas tiles and the settings
/// panel all name a block, and they have to name it the same way.
class BlockPresentation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Every type id the table has, in the order the library reads.
    Q_PROPERTY(QStringList types READ types CONSTANT)
    /// The categories, in the order the library shows them.
    Q_PROPERTY(QStringList categories READ categories CONSTANT)

public:
    explicit BlockPresentation(QObject *parent = nullptr);

    [[nodiscard]] QStringList types() const;
    [[nodiscard]] QStringList categories() const;

    /// What a category heading says.
    [[nodiscard]] Q_INVOKABLE QString categoryName(const QString &category) const;
    /// What a connector says it is for, by its lane's stable id.
    [[nodiscard]] Q_INVOKABLE QString laneName(const QString &laneId) const;

    /// Everything about one block in one map — name, description, tooltip,
    /// category and icon — for a delegate that would otherwise call five
    /// functions with the same argument. Empty for an unknown type.
    [[nodiscard]] Q_INVOKABLE QVariantMap describe(const QString &typeId) const;

    [[nodiscard]] Q_INVOKABLE QString name(const QString &typeId) const;
    [[nodiscard]] Q_INVOKABLE QString description(const QString &typeId) const;
    [[nodiscard]] Q_INVOKABLE QString tooltip(const QString &typeId) const;
    [[nodiscard]] Q_INVOKABLE QString category(const QString &typeId) const;
    [[nodiscard]] Q_INVOKABLE QUrl icon(const QString &typeId) const;
};

} // namespace rb::ui
