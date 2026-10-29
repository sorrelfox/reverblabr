// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <chrono>
#include <string_view>

namespace rb::notify
{

/// **What Reverblabr tells the desktop about**, one per event of
/// `reverblabr.notifyrc`, which is where a person turns each one on or off
/// in System Settings → Notifications.
enum class Event {
    RecordingFinished,
    DeviceLost,
    CaptureLost,
    CaptureRegained,
    PluginFailed,
    LowDiskSpace,
    FeedbackLoop,
    Count, ///< Not an event; the number of them.
};

/// The event's name in `reverblabr.notifyrc`, `[Event/<name>]`. Released
/// names never change: a person's settings are kept under them.
inline constexpr std::array<std::string_view, static_cast<std::size_t>(Event::Count)> kEventIds{
    "recordingFinished",
    "deviceLost",
    "captureLost",
    "captureRegained",
    "pluginFailed",
    "lowDiskSpace",
    "feedbackLoop",
};

[[nodiscard]] QString eventId(Event event);

/// **The bus name a window that sends notifications holds**, queued so
/// that any number of windows can: the daemon sends its own only while
/// nobody holds it, so a person hears each thing once whether a window is
/// open or not.
inline constexpr std::string_view kWindowNotifierName = "io.github.reverblabr.Notifier";

/// One notification, in the person's language.
struct Notification {
    Event event = Event::Count;
    QString title;
    QString text;
    /// The recording, for Open and Show in Folder; empty for everything
    /// else, which has no actions.
    QString file;
};

/// The words of the two actions a finished recording offers.
[[nodiscard]] QString openActionText();
[[nodiscard]] QString showInFolderActionText();

/// **Whether the person wants \a event to pop up**, for a sender that has no
/// KNotifications to ask (the daemon): the `Action` of `[Event/<id>]` in
/// their `reverblabr.notifyrc` if they changed it in System Settings, else
/// in the one installed with Reverblabr, else yes.
[[nodiscard]] bool popupWanted(Event event);

/// **Turns what the engine says into notifications worth sending**.
/// It is told the facade's error(), notice() and recordingFileFinished(),
/// and emits notify() for the ones a person should hear about, in words.
///
/// **Not every change is news**:
/// - a device that is back within a few seconds (a USB device
///   re-enumerating, a Bluetooth headset reconnecting) was never "lost";
/// - an application that falls quiet for a moment between two sounds was
///   never "lost" either, and one whose loss was not said is not "regained";
/// - the same thing about the same block is said once a minute at most.
///
/// Holds no engine and no desktop, so that the window and the daemon make
/// the same decisions, and a test can make them with a clock of its own.
class NotificationPolicy : public QObject
{
    Q_OBJECT

public:
    struct Delays {
        /// How long a device must stay gone to be "lost".
        std::chrono::milliseconds deviceGrace{3000};
        /// How long an application must stay quiet to be "lost".
        std::chrono::milliseconds captureGrace{5000};
        /// How soon the same event about the same thing may be said again.
        std::chrono::milliseconds repeat{60000};
    };

    explicit NotificationPolicy(QObject *parent = nullptr);
    explicit NotificationPolicy(Delays delays, QObject *parent = nullptr);
    ~NotificationPolicy() override;

public Q_SLOTS:
    /// The facade's error(): a plugin that failed, a disk filling up, a
    /// feedback loop refused. Every other error is the window's to show.
    void error(const QString &code, const QString &argsJson);
    /// The facade's notice(): devices and captures going and coming back.
    void notice(const QString &code, const QString &argsJson);
    /// The facade's recordingFileFinished().
    void recordingFileFinished(const QString &sessionId,
                               const QString &blockId,
                               const QString &path,
                               const QString &detailsJson);

Q_SIGNALS:
    void notify(const rb::notify::Notification &notification);

private:
    void hold(Event event, const QString &key, Notification notification, std::chrono::milliseconds grace);
    bool release(Event event, const QString &key);
    /// Emits \a notification unless the same was said about \a key too
    /// recently; whether it did.
    bool send(const QString &key, const Notification &notification);

    Delays m_delays;
    /// Waiting to see whether a loss lasts, by event and key; the value is
    /// how many times the key has been held, so a timer from an earlier
    /// hold finds out it is stale.
    QHash<QString, quint64> m_pending;
    QHash<QString, Notification> m_held;
    quint64 m_holds = 0;
    /// Captures whose loss was said, by key: only those are "regained".
    QSet<QString> m_lostCaptures;
    /// When each event about each key was last said.
    QHash<QString, QElapsedTimer> m_said;
};

} // namespace rb::notify
