// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QQmlEngine>

namespace rb::ui
{

/// Which modifier keys are held, for a button whose click means more with
/// one: a Mixer fade that takes longer with Shift, a solo that is exclusive
/// with Alt. A Button's clicked() carries no event to read them from.
class Keyboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Keyboard(QObject *parent = nullptr);

    /// The modifiers as of the last input event, as Qt.KeyboardModifiers.
    [[nodiscard]] Q_INVOKABLE int modifiers() const;
};

} // namespace rb::ui
