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
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace rb::ui
{

/// **The diagnostics page and the bundle a bug report carries**.
///
/// Asks the engine for what it knows about itself (GetDiagnostics) when the
/// page asks, holds the answer for the page to show, and writes it out as
/// one block of text for the clipboard: versions, the audio system, each
/// session's load and late cycles, whether it runs real-time, our part of
/// the graph, the recording formats, the plugin scan, and the last lines
/// the engine logged, and the window's own too when the engine runs in
/// another process.
///
/// **The bundle is in English, and never translated.** It is read by
/// whoever answers the bug report, who needs the same words from every
/// reporter, the way `reverblabr-ctl --json` is never translated. The page
/// around it is.
///
/// **Personal details are taken out** of the bundle where they can be told
/// apart: the home folder, the user name and the machine's name.
class Diagnostics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// Whether the engine has answered since the page first asked.
    Q_PROPERTY(bool ready READ isReady NOTIFY changed)
    /// What the engine is and runs on: process, pipeWireHeaders,
    /// pipeWireLibrary.
    Q_PROPERTY(QVariantMap engine READ engine NOTIFY changed)
    /// Each open session: name, state, running, and while it runs dspLoad,
    /// peakDspLoad, xruns, lostCycles, rate, quantum, realtime (true, false
    /// or undefined until known), and xrunHistory.
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    /// Our nodes: id, name, description, mediaClass.
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
    /// The links with one of our nodes at either end: from and to, as
    /// "node:port".
    Q_PROPERTY(QVariantList links READ links NOTIFY changed)
    /// Every format a Recorder can write: id, available, reason.
    Q_PROPERTY(QVariantList recordingFormats READ recordingFormats NOTIFY changed)
    /// What the plugin scan found: scanning, formats (format to count),
    /// total, blocked.
    Q_PROPERTY(QVariantMap plugins READ plugins NOTIFY changed)
    /// How many log lines the bundle carries.
    Q_PROPERTY(int logLines READ logLines NOTIFY changed)

public:
    explicit Diagnostics(QObject *parent = nullptr);
    ~Diagnostics() override;

    static Diagnostics *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade. A test gives its own. Nothing is asked until
    /// refresh(): the page is what wants it, and only while it is shown.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] QVariantMap engine() const;
    [[nodiscard]] QVariantList sessions() const;
    [[nodiscard]] QVariantList nodes() const;
    [[nodiscard]] QVariantList links() const;
    [[nodiscard]] QVariantList recordingFormats() const;
    [[nodiscard]] QVariantMap plugins() const;
    [[nodiscard]] int logLines() const;

    /// Asks the engine again.
    Q_INVOKABLE void refresh();
    /// The bundle, redacted, as copyBundle() puts it on the clipboard.
    [[nodiscard]] Q_INVOKABLE QString bundle() const;
    /// Puts bundle() on the clipboard.
    Q_INVOKABLE void copyBundle() const;

    /// **The bundle's text from an answer**, before anything is taken out
    /// of it: \a windowLog is the window's own lines, left out when they are
    /// the engine's (an engine in this process logs through this process).
    [[nodiscard]] static QString bundleText(const QJsonObject &diagnostics,
                                            const QStringList &windowLog,
                                            const QString &windowVersion,
                                            const QString &generatedAt);
    /// \a text with \a home written "~", and \a user and \a host, as whole
    /// words, written "<user>" and "<host>". An empty one is left alone.
    [[nodiscard]] static QString
    redact(const QString &text, const QString &home, const QString &user, const QString &host);

    /// Where copyBundle() puts the text instead of the clipboard, or the
    /// clipboard again when it is empty. **A test's**, so that no test ever
    /// writes to the clipboard of the desktop it happens to run on.
    static void setClipboardSinkForTesting(std::function<void(const QString &)> sink);

Q_SIGNALS:
    void changed();

private:
    void setAnswer(const QJsonObject &json);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
