// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "diagnostics.h"

#include "appshell.h"
#include "core/logring.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QPointer>
#include <QRegularExpression>
#include <QSysInfo>

#include <cmath>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

[[nodiscard]] std::function<void(const QString &)> &clipboardSink()
{
    static std::function<void(const QString &)> sink;
    return sink;
}

[[nodiscard]] QString percent(double share)
{
    return QString::number(std::lround(share * 100.0)) + u'%';
}

[[nodiscard]] QString orDash(const QString &text)
{
    return text.isEmpty() ? u"-"_s : text;
}

/// The user name to take out of a bundle: the environment's, or the home
/// folder's last part when the environment does not say.
[[nodiscard]] QString userName()
{
    const QString fromEnvironment = qEnvironmentVariable("USER");
    return fromEnvironment.isEmpty() ? QDir(QDir::homePath()).dirName() : fromEnvironment;
}

void appendSection(QStringList &lines, const QString &title)
{
    lines.append(QString());
    lines.append(u"== "_s + title + u" =="_s);
}

} // namespace

class Diagnostics::Private
{
public:
    QPointer<core::EngineFacade> facade;
    bool ready = false;
    QJsonObject answer;
};

Diagnostics::Diagnostics(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // Attached here as well as in create(), for the reason the block
    // catalogue is: however QML makes the singleton, it has an engine.
    attach(AppShell::engine());
}

Diagnostics::~Diagnostics() = default;

Diagnostics *Diagnostics::create(QQmlEngine *, QJSEngine *)
{
    return new Diagnostics;
}

void Diagnostics::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade == nullptr) {
        return;
    }
    connect(facade, &core::EngineFacade::diagnostics, this, [this](const QString &json) {
        setAnswer(QJsonDocument::fromJson(json.toUtf8()).object());
    });
}

void Diagnostics::refresh()
{
    if (d->facade) {
        d->facade->getDiagnostics();
    }
}

void Diagnostics::setAnswer(const QJsonObject &json)
{
    d->answer = json;
    d->ready = true;
    Q_EMIT changed();
}

bool Diagnostics::isReady() const
{
    return d->ready;
}

QVariantMap Diagnostics::engine() const
{
    return d->answer.value("engine"_L1).toObject().toVariantMap();
}

QVariantList Diagnostics::sessions() const
{
    return d->answer.value("sessions"_L1).toArray().toVariantList();
}

QVariantList Diagnostics::nodes() const
{
    return d->answer.value("graph"_L1).toObject().value("nodes"_L1).toArray().toVariantList();
}

QVariantList Diagnostics::links() const
{
    return d->answer.value("graph"_L1).toObject().value("links"_L1).toArray().toVariantList();
}

QVariantList Diagnostics::recordingFormats() const
{
    return d->answer.value("recordingFormats"_L1).toArray().toVariantList();
}

QVariantMap Diagnostics::plugins() const
{
    return d->answer.value("plugins"_L1).toObject().toVariantMap();
}

int Diagnostics::logLines() const
{
    return static_cast<int>(d->answer.value("log"_L1).toArray().size());
}

QString Diagnostics::bundle() const
{
    // **The window's own lines only when they are not the engine's**: an
    // engine in this process logs through this process, and its answer
    // already carries every line.
    const qint64 enginePid = d->answer.value("engine"_L1).toObject().value("pid"_L1).toInteger(-1);
    const QStringList windowLog =
        enginePid == QCoreApplication::applicationPid() ? QStringList{} : core::LogRing::lines();
    const QString text = bundleText(d->answer,
                                    windowLog,
                                    QCoreApplication::applicationVersion(),
                                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return redact(text, QDir::homePath(), userName(), QSysInfo::machineHostName());
}

void Diagnostics::copyBundle() const
{
    const QString text = bundle();
    if (clipboardSink()) {
        clipboardSink()(text);
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(text);
    }
}

void Diagnostics::setClipboardSinkForTesting(std::function<void(const QString &)> sink)
{
    clipboardSink() = std::move(sink);
}

QString Diagnostics::bundleText(const QJsonObject &diagnostics,
                                const QStringList &windowLog,
                                const QString &windowVersion,
                                const QString &generatedAt)
{
    QStringList lines;
    lines.append(u"Reverblabr diagnostics"_s);
    lines.append(u"Generated: "_s + generatedAt);
    lines.append(u"Format: "_s + QString::number(diagnostics.value("version"_L1).toInt()));

    const QJsonObject engine = diagnostics.value("engine"_L1).toObject();
    appendSection(lines, u"Versions"_s);
    lines.append(u"Window: reverblabr "_s + orDash(windowVersion) + u", Qt "_s + QString::fromLatin1(qVersion()));
    // **The engine's own version**: a window and a daemon from
    // different builds are the first thing a bug report has to rule out.
    lines.append(u"Engine: "_s + orDash(engine.value("process"_L1).toString()) + u' '
                 + orDash(engine.value("version"_L1).toString()) + u" (pid "_s
                 + QString::number(engine.value("pid"_L1).toInteger()) + u')');
    lines.append(u"PipeWire library: "_s + orDash(engine.value("pipeWireLibrary"_L1).toString()) + u", built with "_s
                 + orDash(engine.value("pipeWireHeaders"_L1).toString()));

    const QJsonObject system = diagnostics.value("audioSystem"_L1).toObject();
    appendSection(lines, u"Audio system"_s);
    lines.append(u"State: "_s + orDash(system.value("state"_L1).toString()));
    lines.append(u"Server: "_s + orDash(system.value("server"_L1).toString()) + u' '
                 + system.value("serverVersion"_L1).toString());
    lines.append(u"Session manager: "_s + orDash(system.value("sessionManager"_L1).toString()) + u' '
                 + system.value("sessionManagerVersion"_L1).toString());
    lines.append(u"Driver: "_s + orDash(system.value("driver"_L1).toString()));
    lines.append(u"Rate: "_s + QString::number(system.value("rate"_L1).toInt()) + u" Hz, quantum "_s
                 + QString::number(system.value("quantum"_L1).toInt()));

    appendSection(lines, u"Sessions"_s);
    const QJsonArray sessions = diagnostics.value("sessions"_L1).toArray();
    if (sessions.isEmpty()) {
        lines.append(u"(none open)"_s);
    }
    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        QString line = session.value("name"_L1).toString() + u" ["_s + session.value("id"_L1).toString() + u"]: "_s
            + session.value("state"_L1).toString();
        if (session.value("running"_L1).toBool()) {
            const QJsonValue realtime = session.value("realtime"_L1);
            line += u", DSP load "_s + percent(session.value("dspLoad"_L1).toDouble()) + u" (peak "_s
                + percent(session.value("peakDspLoad"_L1).toDouble()) + u"), xruns "_s
                + QString::number(session.value("xruns"_L1).toInteger()) + u", lost cycles "_s
                + QString::number(session.value("lostCycles"_L1).toInteger()) + u", "_s
                + QString::number(session.value("rate"_L1).toInteger()) + u" Hz, quantum "_s
                + QString::number(session.value("quantum"_L1).toInteger()) + u", real-time "_s
                + (realtime.isBool() ? (realtime.toBool() ? u"yes"_s : u"NO"_s) : u"not known yet"_s);
            if (const qint64 notRealtime = session.value("longRunsNotRealtime"_L1).toInteger(); notRealtime > 0) {
                line += u", "_s + QString::number(notRealtime) + u" long runs without real-time"_s;
            }
        }
        lines.append(line);
        for (const QJsonValue &event : session.value("xrunHistory"_L1).toArray()) {
            const QJsonObject xrun = event.toObject();
            lines.append(u"  late at "_s + xrun.value("at"_L1).toString() + u": +"_s
                         + QString::number(xrun.value("added"_L1).toInteger()) + u" (total "_s
                         + QString::number(xrun.value("total"_L1).toInteger()) + u')');
        }
    }

    appendSection(lines, u"Recording formats"_s);
    for (const QJsonValue &value : diagnostics.value("recordingFormats"_L1).toArray()) {
        const QJsonObject format = value.toObject();
        lines.append(format.value("id"_L1).toString() + u": "_s
                     + (format.value("available"_L1).toBool()
                            ? u"yes"_s
                            : u"no ("_s + format.value("reason"_L1).toString() + u')'));
    }

    const QJsonObject plugins = diagnostics.value("plugins"_L1).toObject();
    appendSection(lines, u"Plugins"_s);
    QStringList counts;
    const QJsonObject formats = plugins.value("formats"_L1).toObject();
    for (auto it = formats.constBegin(); it != formats.constEnd(); ++it) {
        counts.append(it.key() + u' ' + QString::number(it.value().toInteger()));
    }
    lines.append(u"Found: "_s + QString::number(plugins.value("total"_L1).toInteger())
                 + (counts.isEmpty() ? QString() : u" ("_s + counts.join(u", "_s) + u')')
                 + (plugins.value("scanning"_L1).toBool() ? u", a scan is under way"_s : QString()));
    for (const QJsonValue &value : plugins.value("blocked"_L1).toArray()) {
        const QJsonObject blocked = value.toObject();
        lines.append(u"Blocked: "_s + blocked.value("format"_L1).toString() + u' ' + blocked.value("path"_L1).toString()
                     + u" ("_s + blocked.value("reason"_L1).toString() + u')');
    }
    // A file a scan got only part of the way through: some of its
    // plugins may be missing.
    for (const QJsonValue &value : plugins.value("incomplete"_L1).toArray()) {
        const QJsonObject incomplete = value.toObject();
        lines.append(u"Incomplete: "_s + incomplete.value("format"_L1).toString() + u' '
                     + incomplete.value("path"_L1).toString());
    }

    // **Our part of a pw-dump**, as JSON: what somebody answering the report
    // would otherwise ask for first.
    appendSection(lines, u"Graph (our nodes, their ports, and links touching them)"_s);
    lines.append(
        QString::fromUtf8(QJsonDocument(diagnostics.value("graph"_L1).toObject()).toJson(QJsonDocument::Indented))
            .trimmed());

    const QJsonArray engineLog = diagnostics.value("log"_L1).toArray();
    appendSection(lines, u"Engine log (last "_s + QString::number(engineLog.size()) + u" lines)"_s);
    if (engineLog.isEmpty()) {
        lines.append(u"(the engine keeps no log)"_s);
    }
    for (const QJsonValue &line : engineLog) {
        lines.append(line.toString());
    }
    if (!windowLog.isEmpty()) {
        appendSection(lines, u"Window log (last "_s + QString::number(windowLog.size()) + u" lines)"_s);
        lines.append(windowLog);
    }
    return lines.join(u'\n') + u'\n';
}

QString Diagnostics::redact(const QString &text, const QString &home, const QString &user, const QString &host)
{
    QString out = text;
    // **The home folder first**, as a path: it holds the user name, and once
    // it is "~" the name left elsewhere is the one to find as a word.
    if (home.size() > 1) {
        out.replace(home, u"~"_s);
    }
    const auto word = [&out](const QString &name, const QString &as) {
        if (name.isEmpty()) {
            return;
        }
        const QRegularExpression pattern(u"(?<![\\w.-])"_s + QRegularExpression::escape(name) + u"(?![\\w-])"_s);
        out.replace(pattern, as);
    };
    word(host, u"<host>"_s);
    word(user, u"<user>"_s);
    return out;
}

} // namespace rb::ui
