// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "notifications.h"

#include "texts.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace rb::notify
{

namespace
{

[[nodiscard]] QJsonObject argumentsOf(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

/// What a notification is about, so that two about the same thing are one:
/// the session and the block, which is unique only within its session.
[[nodiscard]] QString keyOf(const QJsonObject &args)
{
    return args.value("session"_L1).toString() + u'/' + args.value("block"_L1).toString();
}

/// **A plugin's id is not a name**: `lv2:http://lsp-plug.in/plugins/lv2/comp`
/// or `clap:in.lsp-plug.comp_mono`. The last part of it is the nearest thing
/// to one that needs no plugin scan to find.
[[nodiscard]] QString pluginNameOf(const QString &plugin)
{
    qsizetype cut = -1;
    for (const QChar separator : {u'/', u'#', u':'}) {
        cut = std::max(cut, plugin.lastIndexOf(separator));
    }
    const QString name = plugin.mid(cut + 1);
    return name.isEmpty() ? plugin : name;
}

[[nodiscard]] QString durationOf(double seconds)
{
    const auto whole = static_cast<qint64>(seconds + 0.5);
    const qint64 hours = whole / 3600;
    const qint64 minutes = (whole / 60) % 60;
    const qint64 rest = whole % 60;
    const QLocale locale;
    const QString mm = locale.toString(minutes).rightJustified(hours > 0 ? 2 : 1, locale.zeroDigit().at(0));
    const QString ss = locale.toString(rest).rightJustified(2, locale.zeroDigit().at(0));
    if (hours > 0) {
        return locale.toString(hours) + u':' + mm + u':' + ss;
    }
    return mm + u':' + ss;
}

[[nodiscard]] QString nameOrUnknown(const QString &name)
{
    return !name.isEmpty() ? name
                           : translate(RB_I18NC("@item a device or application whose name is not known", "unknown"));
}

} // namespace

QString eventId(Event event)
{
    const std::string_view id = kEventIds[static_cast<std::size_t>(event)];
    return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
}

QString openActionText()
{
    return translate(RB_I18NC("@action:button in a notification that a recording finished", "Open"));
}

QString showInFolderActionText()
{
    return translate(RB_I18NC("@action:button in a notification that a recording finished", "Show in Folder"));
}

bool popupWanted(Event event)
{
    const QString key = u"Event/"_s + eventId(event) + u"/Action"_s;
    const QString mine =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/reverblabr.notifyrc"_s;
    const QString installed =
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, u"knotifications6/reverblabr.notifyrc"_s);
    for (const QString &file : {mine, installed}) {
        if (file.isEmpty() || !QFileInfo::exists(file)) {
            continue;
        }
        const QSettings settings(file, QSettings::IniFormat);
        if (settings.contains(key)) {
            // A list such as "Popup|Sound", or empty for none of them.
            return settings.value(key).toString().split(u'|').contains("Popup"_L1);
        }
    }
    return true;
}

NotificationPolicy::NotificationPolicy(QObject *parent)
    : NotificationPolicy(Delays{}, parent)
{
}

NotificationPolicy::NotificationPolicy(Delays delays, QObject *parent)
    : QObject(parent)
    , m_delays(delays)
{
}

NotificationPolicy::~NotificationPolicy() = default;

void NotificationPolicy::error(const QString &code, const QString &argsJson)
{
    const QJsonObject args = argumentsOf(argsJson);
    Notification notification;
    if (code == "plugin.crashed"_L1 || code == "plugin.stoppedResponding"_L1) {
        notification.event = Event::PluginFailed;
        const QString plugin = pluginNameOf(args.value("plugin"_L1).toString());
        notification.title = translate(RB_I18NC("@title a notification", "Plugin stopped"));
        notification.text = code == "plugin.crashed"_L1
            ? translate(RB_I18NC("@info %1 is a plugin's name",
                                 "%1 crashed. Its block is bypassed, and it can be restarted from its panel."))
                  .arg(plugin)
            : translate(RB_I18NC("@info %1 is a plugin's name",
                                 "%1 stopped responding. Its block is bypassed, and it can be restarted "
                                 "from its panel."))
                  .arg(plugin);
    } else if (code == "recording.lowDiskSpace"_L1) {
        notification.event = Event::LowDiskSpace;
        const qint64 megabytes = args.value("megabytes"_L1).toString().toLongLong();
        notification.title = translate(RB_I18NC("@title a notification", "Disk nearly full"));
        notification.text = translate(RB_I18NC("@info %1 is a recording's file name; %2 is an amount of disk space",
                                               "Only %2 is left on the disk %1 is recorded to. The recording goes "
                                               "on, and stops cleanly before the disk is full."))
                                .arg(QFileInfo(args.value("file"_L1).toString()).fileName(),
                                     QLocale().formattedDataSize(megabytes * 1024 * 1024));
    } else if (code == "routing.feedbackLoop"_L1) {
        notification.event = Event::FeedbackLoop;
        notification.title = translate(RB_I18NC("@title a notification", "Feedback prevented"));
        notification.text = translate(RB_I18NC("@info",
                                               "A connection would have fed a session's sound back into itself, so "
                                               "it wasn't made."));
    } else {
        return;
    }
    (void)send(eventId(notification.event) + u'@' + keyOf(args), notification);
}

void NotificationPolicy::notice(const QString &code, const QString &argsJson)
{
    const QJsonObject args = argumentsOf(argsJson);
    const QString key = keyOf(args);
    const QString session = args.value("session"_L1).toString();
    if (code == "device.lost"_L1) {
        Notification notification;
        notification.event = Event::DeviceLost;
        notification.title = translate(RB_I18NC("@title a notification", "Device disconnected"));
        notification.text = translate(RB_I18NC("@info %1 is a device's name; %2 is a session's name",
                                               "%1 went away while %2 was running. The session waits for it and "
                                               "carries on when it is back."))
                                .arg(nameOrUnknown(args.value("device"_L1).toString()), session);
        hold(Event::DeviceLost, key, notification, m_delays.deviceGrace);
    } else if (code == "device.returned"_L1) {
        (void)release(Event::DeviceLost, key);
    } else if (code == "capture.lost"_L1) {
        Notification notification;
        notification.event = Event::CaptureLost;
        notification.title = translate(RB_I18NC("@title a notification", "Application stopped playing"));
        notification.text = translate(RB_I18NC("@info %1 is an application's name; %2 is a session's name",
                                               "%1 is no longer playing into %2."))
                                .arg(nameOrUnknown(args.value("application"_L1).toString()), session);
        hold(Event::CaptureLost, key, notification, m_delays.captureGrace);
    } else if (code == "capture.regained"_L1) {
        // Back before its loss was said: it was never lost.
        if (release(Event::CaptureLost, key) || !m_lostCaptures.remove(key)) {
            return;
        }
        Notification notification;
        notification.event = Event::CaptureRegained;
        notification.title = translate(RB_I18NC("@title a notification", "Application playing again"));
        notification.text = translate(RB_I18NC("@info %1 is an application's name; %2 is a session's name",
                                               "%1 is playing into %2 again."))
                                .arg(nameOrUnknown(args.value("application"_L1).toString()), session);
        (void)send(eventId(Event::CaptureRegained) + u'@' + key, notification);
    }
}

void NotificationPolicy::recordingFileFinished(const QString &sessionId,
                                               const QString &blockId,
                                               const QString &path,
                                               const QString &detailsJson)
{
    Q_UNUSED(sessionId)
    Q_UNUSED(blockId)
    Notification notification;
    notification.event = Event::RecordingFinished;
    notification.file = path;
    notification.title = translate(RB_I18NC("@title a notification", "Recording finished"));
    notification.text =
        translate(RB_I18NC("@info %1 is a recording's file name; %2 how long it is, as 1:05", "%1 is saved, %2 long."))
            .arg(QFileInfo(path).fileName(), durationOf(argumentsOf(detailsJson).value("seconds"_L1).toDouble()));
    // Every file is its own news.
    (void)send(eventId(Event::RecordingFinished) + u'@' + path, notification);
}

void NotificationPolicy::hold(Event event,
                              const QString &key,
                              Notification notification,
                              std::chrono::milliseconds grace)
{
    const QString pending = eventId(event) + u'@' + key;
    const quint64 hold = ++m_holds;
    m_pending.insert(pending, hold);
    m_held.insert(pending, std::move(notification));
    QTimer::singleShot(grace, this, [this, pending, hold, event, key] {
        if (m_pending.value(pending) != hold) {
            return;
        }
        m_pending.remove(pending);
        const Notification held = m_held.take(pending);
        // Only a loss that was said can come back.
        if (send(pending, held) && event == Event::CaptureLost) {
            m_lostCaptures.insert(key);
        }
    });
}

bool NotificationPolicy::release(Event event, const QString &key)
{
    const QString pending = eventId(event) + u'@' + key;
    m_held.remove(pending);
    return m_pending.remove(pending) > 0;
}

bool NotificationPolicy::send(const QString &key, const Notification &notification)
{
    if (const auto said = m_said.constFind(key);
        said != m_said.constEnd() && said->isValid() && said->elapsed() < m_delays.repeat.count()) {
        return false;
    }
    QElapsedTimer now;
    now.start();
    m_said.insert(key, now);
    Q_EMIT notify(notification);
    return true;
}

} // namespace rb::notify
