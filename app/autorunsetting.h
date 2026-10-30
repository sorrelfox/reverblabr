// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QQmlEngine>

namespace rb::ui
{

/// When the background engine runs the Auto Run sessions ([ADR-0023]):
/// the first time it starts in a login (and after a crash), so that a
/// session the person stopped stays stopped until the next login, or every
/// time it starts. The Settings menu's Auto Run choice.
///
/// **One choice for every session**, unlike Auto Run itself, which is each
/// session's own: it is about how this person wants the engine to behave,
/// not about what a session does.
///
/// **In the settings file, which the daemon reads as it starts**
/// (core::autoRunStart), so a change holds from the engine's next start.
/// Nothing has to be said to an engine that is running: the choice is
/// only ever about what an engine does as it starts.
class AutoRunSetting : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether every engine start runs the Auto Run sessions; false, the
    /// default, for once a login.
    Q_PROPERTY(bool everyEngineStart READ everyEngineStart WRITE setEveryEngineStart NOTIFY everyEngineStartChanged)

public:
    explicit AutoRunSetting(QObject *parent = nullptr);

    [[nodiscard]] bool everyEngineStart() const;
    void setEveryEngineStart(bool every);

    /// Where to read and write. A test gives its own, so that running the
    /// tests does not reach into whoever is running them.
    static void setConfig(const KSharedConfig::Ptr &config);

Q_SIGNALS:
    void everyEngineStartChanged();

private:
    bool m_everyEngineStart = false;
};

} // namespace rb::ui
