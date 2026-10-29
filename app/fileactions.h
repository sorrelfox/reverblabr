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
#include <QUrl>
#include <QWindow>

#include <functional>

namespace rb::ui
{

/// What the Files tab does with a recording, through KIO: the
/// desktop's own file manager, its own "Open With" and its own trash.
///
/// **The desktop's, not ours.** Open Containing Folder asks the file
/// manager over the freedesktop FileManager1 interface (Dolphin on Plasma)
/// to open the folder with the file selected; Open With… is the desktop's
/// application chooser; Move to Trash is the freedesktop trash, where the
/// file can be put back from — **never a delete**.
///
/// **Tests never reach the desktop.** A QML test run, and a unit test,
/// hands setRunnerForTesting() a stand-in that is told which action on
/// which file and answers for it, so no test opens a file manager, starts
/// an application or touches anybody's trash.
class FileActions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum class Action {
        ShowInFolder,
        OpenWith,
        MoveToTrash,
        /// Opens the file with the application the desktop opens its type
        /// with, as a double click in the file manager would: a finished
        /// recording's notification offers it.
        Open,
    };
    Q_ENUM(Action)

    /// The job's answer: empty when it worked, or KIO's message.
    using Done = std::function<void(const QString &error)>;
    /// Carries out \a action on \a url, and calls done exactly once.
    using Runner = std::function<void(Action action, const QUrl &url, QWindow *window, Done done)>;

    explicit FileActions(QObject *parent = nullptr);

    /// Opens the file manager at \a path's folder, with the file selected.
    Q_INVOKABLE void showInFolder(const QString &path, QWindow *window = nullptr);
    /// Offers the desktop's list of applications that open \a path.
    Q_INVOKABLE void openWith(const QString &path, QWindow *window = nullptr);
    /// Opens \a path with whatever the desktop opens it with.
    Q_INVOKABLE void open(const QString &path, QWindow *window = nullptr);
    /// Moves \a path to the trash. trashed() or failed() says how it went;
    /// once it is there, the recordings index is asked to check the disk,
    /// so the file leaves the list.
    Q_INVOKABLE void moveToTrash(const QString &path, QWindow *window = nullptr);

    /// Replaces KIO for a whole test run. Pass nothing to put KIO back.
    static void setRunnerForTesting(Runner runner);

    /// What KIO itself does, for a test that runs one job for real — a
    /// trash under a home of its own.
    static void runWithKio(Action action, const QUrl &url, QWindow *window, const Done &done);

Q_SIGNALS:
    void trashed(const QString &path);
    /// \a action on \a path did not work, and \a error says why, in KIO's
    /// words — which are already translated.
    void failed(const QString &path, rb::ui::FileActions::Action action, const QString &error);

private:
    void run(Action action, const QString &path, QWindow *window);
};

} // namespace rb::ui
