// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "keyboard.h"

#include <QGuiApplication>

namespace rb::ui
{

Keyboard::Keyboard(QObject *parent)
    : QObject(parent)
{
}

int Keyboard::modifiers() const
{
    return static_cast<int>(QGuiApplication::keyboardModifiers().toInt());
}

} // namespace rb::ui
