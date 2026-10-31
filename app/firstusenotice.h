// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QQmlEngine>

namespace rb::ui
{

/// Whether the words shown before the first Hearing block have been read
/// and gone past.
///
/// **In the settings file, not a session**: the words are said to the person
/// once, however many sessions they make, and a session file handed to
/// somebody else must not carry the answer with it.
class FirstUseNotice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// False until somebody goes on past the notice. Cancelling leaves it
    /// false, so the next add asks again.
    Q_PROPERTY(bool hearingAccepted READ hearingAccepted WRITE setHearingAccepted NOTIFY hearingAcceptedChanged)

public:
    explicit FirstUseNotice(QObject *parent = nullptr);

    [[nodiscard]] bool hearingAccepted() const;
    void setHearingAccepted(bool accepted);

    /// Where to read and write. A test gives its own, so that running the
    /// tests does not reach into whoever is running them.
    static void setConfig(const KSharedConfig::Ptr &config);

Q_SIGNALS:
    void hearingAcceptedChanged();
};

} // namespace rb::ui
