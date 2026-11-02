// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "modelfolderreader.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// What model::Refusal::NoHost is: the program could not be asked.
constexpr int kNoHost = 12;
/// What it says of a folder the program took too long over, as it would
/// of a file that broke it (model::Refusal::NotAModel).
constexpr int kNotAModel = 5;
constexpr int kAnswerWithinMs = 5000;
/// The most an answer can be.
constexpr qint64 kLongestAnswer = 1 << 14;

struct Reading {
    int refusal = 0;
    int family = 0;
    QString modelFile;
    QString licenceFile;
    int sampleRate = 0;
    int channels = 0;
    int hopSize = 0;
};

} // namespace

class ModelFolderReader::Private
{
public:
    QString folder;
    Reading reading;
    std::unique_ptr<QProcess> process;
    QTimer deadline;
};

ModelFolderReader::ModelFolderReader(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->deadline.setSingleShot(true);
    d->deadline.setInterval(kAnswerWithinMs);
    connect(&d->deadline, &QTimer::timeout, this, [this] {
        if (d->process) {
            d->process->kill();
        }
    });
}

ModelFolderReader::~ModelFolderReader()
{
    if (d->process) {
        d->process->disconnect(this);
        d->process->kill();
        d->process->waitForFinished(1000);
    }
}

QString ModelFolderReader::program()
{
    if (const QString chosen = qEnvironmentVariable("REVERBLABR_MODEL_HOST"); !chosen.isEmpty()) {
        return chosen;
    }
    const QString installed = QStringLiteral(RB_MODEL_HOST_INSTALLED);
    return QFileInfo(installed).isExecutable() ? installed : QStringLiteral(RB_MODEL_HOST_BUILT);
}

QString ModelFolderReader::folder() const
{
    return d->folder;
}

void ModelFolderReader::setFolder(const QString &folder)
{
    if (folder == d->folder) {
        return;
    }
    d->folder = folder;
    Q_EMIT folderChanged();
    refresh();
}

void ModelFolderReader::refresh()
{
    if (d->process) {
        d->process->disconnect(this);
        d->process->kill();
        d->process->waitForFinished(1000);
        d->process.reset();
    }
    d->reading = {};
    if (d->folder.isEmpty()) {
        Q_EMIT changed();
        return;
    }
    d->process = std::make_unique<QProcess>();
    d->process->setStandardErrorFile(QProcess::nullDevice());
    connect(d->process.get(), &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QByteArray text = d->process->read(kLongestAnswer);
        d->deadline.stop();
        d->process.release()->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            takeAnswer(text);
            return;
        }
        d->reading = {};
        d->reading.refusal = status == QProcess::CrashExit ? kNotAModel : kNoHost;
        Q_EMIT changed();
    });
    connect(d->process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        d->deadline.stop();
        d->process.release()->deleteLater();
        d->reading = {};
        d->reading.refusal = kNoHost;
        Q_EMIT changed();
    });
    d->deadline.start();
    d->process->start(program(), {u"--inspect"_s, QDir::fromNativeSeparators(QDir::cleanPath(d->folder))});
    Q_EMIT changed();
}

void ModelFolderReader::takeAnswer(const QByteArray &text)
{
    Reading reading;
    reading.refusal = kNoHost;
    for (const QByteArray &line : text.split('\n')) {
        const qsizetype equals = line.indexOf('=');
        if (equals <= 0) {
            continue;
        }
        const QByteArray key = line.first(equals);
        const QByteArray value = line.sliced(equals + 1);
        if (key == "refusal") {
            reading.refusal = value.toInt();
        } else if (key == "family") {
            reading.family = value.toInt();
        } else if (key == "modelFile") {
            reading.modelFile = QString::fromUtf8(value);
        } else if (key == "licenceFile") {
            reading.licenceFile = QString::fromUtf8(value);
        } else if (key == "sampleRate") {
            reading.sampleRate = value.toInt();
        } else if (key == "channels") {
            reading.channels = value.toInt();
        } else if (key == "hopSize") {
            reading.hopSize = value.toInt();
        }
    }
    d->reading = reading;
    Q_EMIT changed();
}

bool ModelFolderReader::busy() const
{
    return d->process != nullptr;
}

int ModelFolderReader::refusal() const
{
    return d->reading.refusal;
}

int ModelFolderReader::family() const
{
    return d->reading.family;
}

QString ModelFolderReader::modelFile() const
{
    return d->reading.modelFile;
}

QString ModelFolderReader::licenceFile() const
{
    return d->reading.licenceFile;
}

int ModelFolderReader::sampleRate() const
{
    return d->reading.sampleRate;
}

int ModelFolderReader::channels() const
{
    return d->reading.channels;
}

int ModelFolderReader::hopSize() const
{
    return d->reading.hopSize;
}

} // namespace rb::ui
