// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "compressormodes.h"

#include "core/compressormodes.h"

namespace rb::ui
{

CompressorModes::CompressorModes(QObject *parent)
    : QObject(parent)
{
}

QStringList CompressorModes::ids() const
{
    return core::CompressorModes::ids();
}

QString CompressorModes::defaultId() const
{
    return core::CompressorModes::defaultId();
}

QVariantMap CompressorModes::settings(const QString &id) const
{
    return core::CompressorModes::settings(id);
}

} // namespace rb::ui
