// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <KAboutData>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>

namespace rb::ui
{

/// What the Help menu needs: who wrote this, where the handbook is, and
/// where a bug goes.
///
/// **One source, which is KAboutData.** The name, the version, the licence
/// and the components are already written down once for the command line,
/// the crash handler and the desktop file; the About window reads the same
/// record rather than keeping a second copy to fall out of step.
class AppInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The application's own record, for Kirigami's About page.
    Q_PROPERTY(KAboutData aboutData READ aboutData CONSTANT)
    /// The handbook, opened by KHelpCenter or whatever handles `help:`.
    Q_PROPERTY(QUrl handbookUrl READ handbookUrl CONSTANT)
    /// Where a bug goes, or an empty URL when this build has nowhere to
    /// send one. **Empty rather than a guess**: a Report Bug that quietly
    /// sends somebody to the wrong tracker is worse than one that is greyed
    /// out.
    Q_PROPERTY(QUrl bugReportUrl READ bugReportUrl CONSTANT)

public:
    explicit AppInfo(QObject *parent = nullptr);

    [[nodiscard]] KAboutData aboutData() const;
    [[nodiscard]] QUrl handbookUrl() const;
    [[nodiscard]] QUrl bugReportUrl() const;
};

} // namespace rb::ui
