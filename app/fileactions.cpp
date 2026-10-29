// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "fileactions.h"

#include "appshell.h"
#include "core/enginefacade.h"

#include <KIO/ApplicationLauncherJob>
#include <KIO/CopyJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KJobWindows>
#include <QPointer>
#include <QWindow>

namespace rb::ui
{

namespace
{

FileActions::Runner &testRunner()
{
    static FileActions::Runner runner;
    return runner;
}

/// Calls \a done with \a job's answer when it has one.
void answerWhenDone(KJob *job, const FileActions::Done &done)
{
    QObject::connect(job, &KJob::result, job, [done](KJob *finished) {
        done(finished->error() == 0 ? QString{} : finished->errorString());
    });
}

} // namespace

FileActions::FileActions(QObject *parent)
    : QObject(parent)
{
}

void FileActions::showInFolder(const QString &path, QWindow *window)
{
    run(Action::ShowInFolder, path, window);
}

void FileActions::openWith(const QString &path, QWindow *window)
{
    run(Action::OpenWith, path, window);
}

void FileActions::open(const QString &path, QWindow *window)
{
    run(Action::Open, path, window);
}

void FileActions::moveToTrash(const QString &path, QWindow *window)
{
    run(Action::MoveToTrash, path, window);
}

void FileActions::setRunnerForTesting(Runner runner)
{
    testRunner() = std::move(runner);
}

void FileActions::runWithKio(Action action, const QUrl &url, QWindow *window, const Done &done)
{
    switch (action) {
    case Action::ShowInFolder: {
        // The freedesktop FileManager1 interface, or the folder opened with
        // whatever opens folders when no file manager answers it.
        KIO::OpenFileManagerWindowJob *job = KIO::highlightInFileManager({url});
        answerWhenDone(job, done);
        return;
    }
    case Action::OpenWith: {
        // **No service given**, which is what makes the job ask: the
        // desktop's "Open With" list, remembered per type as it always is.
        auto *job = new KIO::ApplicationLauncherJob;
        job->setUrls({url});
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
        // The chooser belongs to the window it was asked from.
        KJobWindows::setWindow(job, window);
        answerWhenDone(job, done);
        job->start();
        return;
    }
    case Action::MoveToTrash: {
        KIO::CopyJob *job = KIO::trash({url});
        KJobWindows::setWindow(job, window);
        answerWhenDone(job, done);
        return;
    }
    case Action::Open: {
        auto *job = new KIO::OpenUrlJob(url);
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
        KJobWindows::setWindow(job, window);
        answerWhenDone(job, done);
        job->start();
        return;
    }
    }
    done(QString{});
}

void FileActions::run(Action action, const QString &path, QWindow *window)
{
    if (path.isEmpty()) {
        return;
    }
    const QUrl url = QUrl::fromLocalFile(path);
    QPointer<FileActions> self(this);
    Done done = [self, action, path](const QString &error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            Q_EMIT self->failed(path, action, error);
            return;
        }
        if (action == Action::MoveToTrash) {
            Q_EMIT self->trashed(path);
            // The file has gone from where the index has it, so the index
            // is asked to look: the one list there is follows the disk.
            if (core::EngineFacade *engine = AppShell::engine()) {
                engine->rescanRecordings();
            }
        }
    };
    if (testRunner()) {
        testRunner()(action, url, window, std::move(done));
        return;
    }
    runWithKio(action, url, window, done);
}

} // namespace rb::ui
