// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace rb::ui
{

/// **Where each Broadcast or Live Stream block's stream stands**, as the engine has said
/// it (the facade's broadcastStatusChanged()): for the Status tab of its
/// panel and the state line above the tabs, and the one place its password
/// is sent from.
///
/// The engine says the state, the numbers and whether a password is
/// stored; this keeps the latest word per block of each session. **The
/// time on the air is counted here**, from when the engine says the
/// connection was made, so that it ticks every second without a signal
/// every second.
class BroadcastStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Goes up whenever anything here changes, for bindings to depend on.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit BroadcastStatus(QObject *parent = nullptr);
    ~BroadcastStatus() override;

    static BroadcastStatus *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// "idle", "waitingForPassword", "connecting", "connected", "retrying"
    /// or "failed".
    [[nodiscard]] Q_INVOKABLE QString state(const QString &sessionId, const QString &blockId) const;
    /// Everything the engine last said, as the interface XML describes it.
    [[nodiscard]] Q_INVOKABLE QVariantMap status(const QString &sessionId, const QString &blockId) const;
    /// Whether the engine has a password for it, once it knows.
    [[nodiscard]] Q_INVOKABLE bool passwordStored(const QString &sessionId, const QString &blockId) const;
    /// How long it has been on the air, in whole seconds; 0 when it is not.
    [[nodiscard]] Q_INVOKABLE int connectedSeconds(const QString &sessionId, const QString &blockId) const;

    /// Asks the engine where the stream stands, for a panel that has just
    /// opened.
    Q_INVOKABLE void request(const QString &sessionId, const QString &blockId);
    /// **Hands a new password to the engine**, which keeps it in the
    /// desktop's secret store; empty removes it. Never kept here.
    Q_INVOKABLE void setPassword(const QString &sessionId, const QString &blockId, const QString &password);

    /// The bit rates the engine streams \a format at with one channel or
    /// two, as `{lowest, highest}` in kilobits a second.
    [[nodiscard]] Q_INVOKABLE static QVariantMap bitrateRange(const QString &format, bool mono);

    /// "12.3 MB", in the locale's way.
    [[nodiscard]] Q_INVOKABLE static QString bytesText(double bytes);

    [[nodiscard]] int revision() const;

    void attach(core::EngineFacade *facade);
    /// The clock, for a test: milliseconds since the epoch.
    void setClock(std::function<qint64()> clock);

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
