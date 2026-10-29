// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace rb::core
{
class EngineFacade;
}

namespace rb::daemon
{

/// Which sessions the daemon has open, written down as they open and close,
/// so that a daemon started again opens them again (ADR-0017).
///
/// **Opened again, never started again**: a daemon that crashed while a
/// session played comes back with it stopped. Starting sessions by
/// themselves is what Auto Run will ask for, per session.
class OpenSessions : public QObject
{
    Q_OBJECT

public:
    /// Follows \a engine, which must outlive this, and keeps the list in
    /// \a path.
    OpenSessions(core::EngineFacade *engine, QString path, QObject *parent = nullptr);

    /// Opens every session the file names.
    void restore();

    /// Stops writing down what closes: **what a daemon closing its sessions
    /// on the way out does is not a person closing them**, and the list has
    /// to survive it.
    void freeze();

    [[nodiscard]] QStringList ids() const;

private:
    void write() const;

    core::EngineFacade *m_engine = nullptr;
    QString m_path;
    QStringList m_ids;
    bool m_frozen = false;
};

} // namespace rb::daemon
