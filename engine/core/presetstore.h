// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/preset.h"

#include <QList>
#include <QString>

#include <expected>

namespace rb::core
{

/// Where presets live, and the reading and writing of them.
///
/// **Two kinds, one list.** Factory presets ship with the application and
/// cannot be written to; user presets live in
/// `$XDG_DATA_HOME/reverblabr/presets/<type>/<name>.json`, one file each, so
/// that a person can copy one to somebody, keep them in version control, or
/// look at what a preset actually is. The interface is offered both together
/// and told which is which.
///
/// **A user preset may take a factory one's id.** It does not replace it:
/// both are listed, and the one a person made is theirs to rename or delete
/// while the shipped one stays where it is. Nothing is ever written over a
/// factory preset, which is the whole of what "read-only" means here.
///
/// Saving is atomic, for the reason SessionStore's is: a preset half written
/// is a preset that will not load, and it would be the one somebody was
/// about to rely on.
class PresetStore
{
public:
    /// Uses `$XDG_DATA_HOME/reverblabr/presets`.
    PresetStore();

    /// Uses \a directory for the user's presets instead, which is what tests
    /// do. The factory presets are the shipped ones either way.
    explicit PresetStore(QString directory);

    /// `$XDG_DATA_HOME/reverblabr/presets`, whether or not it exists.
    [[nodiscard]] static QString defaultDirectory();

    [[nodiscard]] const QString &directory() const noexcept
    {
        return m_directory;
    }

    /// Where the file for a user preset of \a typeId called \a id is,
    /// whether or not it exists. **For showing it in a file manager**, which
    /// is a thing the panel offers, and for a test.
    [[nodiscard]] QString pathFor(QStringView typeId, QStringView id) const;

    /// Every preset for \a typeId: the shipped ones first, in the order they
    /// ship, then the person's own in name order.
    ///
    /// **The order is not alphabetical across both.** A factory preset is
    /// part of what the block is, and burying "Default" between two of
    /// somebody's own would make the list read as a pile rather than as a
    /// starting point and then their work.
    [[nodiscard]] QList<Preset> presetsFor(QStringView typeId) const;

    /// One preset by type and id, factory or user. A user preset wins over
    /// a factory one of the same id, because it is the one that was saved
    /// most recently by the person asking.
    [[nodiscard]] std::expected<Preset, PresetError> preset(QStringView typeId, QStringView id) const;

    /// Writes a user preset. \a replace says whether an existing one of that
    /// name may be written over; without it, saving onto one is refused so
    /// that the interface can ask first.
    ///
    /// The id is taken from the name, which is what the file is called.
    [[nodiscard]] std::expected<Preset, PresetError> save(const Preset &preset, bool replace = false) const;

    /// Renames a user preset, which moves its file. Refused for a factory
    /// preset and for a name something else already has.
    [[nodiscard]] std::expected<Preset, PresetError> rename(QStringView typeId, QStringView id, QStringView name) const;

    /// Removes a user preset. Refused for a factory one.
    [[nodiscard]] std::expected<void, PresetError> remove(QStringView typeId, QStringView id) const;

    /// The presets that ship with the application, for \a typeId. **Read
    /// from a resource compiled into the binary**, so there is no file for
    /// anybody to edit and no installation step that can leave them out.
    [[nodiscard]] static QList<Preset> factoryPresetsFor(QStringView typeId);

private:
    QString m_directory;
};

} // namespace rb::core
