// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QProcess>
#include <QQmlEngine>
#include <QString>

#include <memory>

namespace rb::ui
{

/// **What a Model block's folder holds**, for its panel (ADR-0031): the
/// family, the shape, the licence file, or why it is refused. Asked of the
/// model host with `--inspect`, as the block asks it: the window never reads
/// a model file itself, and a file that breaks the reader breaks a short
/// process and nothing else.
class ModelFolderReader : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The folder to look at, as the session holds it.
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    /// Whether an answer is on its way.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// model::Refusal, and model::Family: numbers, whose words are ModelState's.
    Q_PROPERTY(int refusal READ refusal NOTIFY changed)
    Q_PROPERTY(int family READ family NOTIFY changed)
    Q_PROPERTY(QString modelFile READ modelFile NOTIFY changed)
    Q_PROPERTY(QString licenceFile READ licenceFile NOTIFY changed)
    /// The two files a voice takes beside its own.
    Q_PROPERTY(QString contentFile READ contentFile NOTIFY changed)
    Q_PROPERTY(QString pitchFile READ pitchFile NOTIFY changed)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY changed)
    Q_PROPERTY(int channels READ channels NOTIFY changed)
    Q_PROPERTY(int hopSize READ hopSize NOTIFY changed)

public:
    explicit ModelFolderReader(QObject *parent = nullptr);
    ~ModelFolderReader() override;

    [[nodiscard]] QString folder() const;
    void setFolder(const QString &folder);
    [[nodiscard]] bool busy() const;
    [[nodiscard]] int refusal() const;
    [[nodiscard]] int family() const;
    [[nodiscard]] QString modelFile() const;
    [[nodiscard]] QString licenceFile() const;
    [[nodiscard]] QString contentFile() const;
    [[nodiscard]] QString pitchFile() const;
    [[nodiscard]] int sampleRate() const;
    [[nodiscard]] int channels() const;
    [[nodiscard]] int hopSize() const;

    /// Takes in what `--inspect` printed: the one thing the host's answer
    /// goes through, so a test can give it one.
    Q_INVOKABLE void takeAnswer(const QByteArray &text);
    /// Looks again, for a folder whose files have changed.
    Q_INVOKABLE void refresh();

    /// The program: `REVERBLABR_MODEL_HOST`, the installed one, or the built.
    [[nodiscard]] static QString program();

Q_SIGNALS:
    void folderChanged();
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
