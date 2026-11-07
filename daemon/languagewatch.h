// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

namespace rb::daemon
{

/// **The daemon speaks the language chosen in the window, from the moment
/// it is chosen** (ADR-0036). The daemon outlives every window, so waiting
/// for its next start, as the window does for itself, could be days: its
/// notifications and what KRunner shows would go on in the old language.
///
/// - **Watches the settings file and its folder**: the window's KConfig
///   writes a new file and renames it over the old one, which a watch on the
///   file alone sees once and then loses.
/// - **Reads the choice again** a moment after anything there changed, as
///   notify::chosenLanguage() reads it, and has notify::ProcessLanguage
///   follow it when it is another language: every word the daemon looks up
///   after that is in the new one. Nothing it said before is said again.
///
/// On the main thread, which is the only one that may change the
/// environment.
class LanguageWatch : public QObject
{
    Q_OBJECT

public:
    /// Follows the choice in \a settingsPath, starting from the language the
    /// process was started in (notify::ProcessLanguage::atStart()).
    explicit LanguageWatch(QString settingsPath, QObject *parent = nullptr);

    /// The language now followed: a code, or empty for the system's.
    [[nodiscard]] QString language() const;

Q_SIGNALS:
    /// The daemon now speaks \a language: a code, or empty for the system's.
    void changed(const QString &language);

private:
    void watch();
    void read();

    QString m_path;
    QString m_language;
    QFileSystemWatcher m_watcher;
    QTimer m_soon;
};

} // namespace rb::daemon
