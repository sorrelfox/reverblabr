// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "languagewatch.h"

#include "language.h"

#include <QFileInfo>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcLanguage, "reverblabr.daemon.language")

namespace rb::daemon
{

namespace
{

/// **How long a burst of changes is gathered**: a write is a new file, a
/// rename and the folder changing, and the file is read once for all of it.
constexpr auto kReadDelay = std::chrono::milliseconds(150);

} // namespace

LanguageWatch::LanguageWatch(QString settingsPath, QObject *parent)
    : QObject(parent)
    , m_path(std::move(settingsPath))
    , m_language(notify::ProcessLanguage::atStart())
{
    m_soon.setSingleShot(true);
    m_soon.setInterval(kReadDelay);
    connect(&m_soon, &QTimer::timeout, this, &LanguageWatch::read);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_soon.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_soon.start(); });
    watch();
    // **A change made while the daemon was starting**, between main()'s read
    // and the watch, is not missed.
    m_soon.start();
}

QString LanguageWatch::language() const
{
    return m_language;
}

void LanguageWatch::watch()
{
    const QString folder = QFileInfo(m_path).path();
    if (!m_watcher.directories().contains(folder) && QFileInfo(folder).isDir() && !m_watcher.addPath(folder)) {
        qCWarning(lcLanguage) << "cannot watch" << folder << "for a change of language";
    }
    // The file again, once it is back: a rename put a new one where the
    // watched one was.
    if (!m_watcher.files().contains(m_path) && QFileInfo::exists(m_path)) {
        m_watcher.addPath(m_path);
    }
}

void LanguageWatch::read()
{
    watch();
    const QString language = notify::chosenLanguage(m_path);
    if (language == m_language) {
        return;
    }
    m_language = language;
    notify::ProcessLanguage::follow(language);
    qCInfo(lcLanguage).noquote() << "now speaking" << (language.isEmpty() ? u"the system's language"_s : language);
    Q_EMIT changed(language);
}

} // namespace rb::daemon
