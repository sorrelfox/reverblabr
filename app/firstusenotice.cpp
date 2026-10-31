// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "firstusenotice.h"

#include <KConfigGroup>

namespace rb::ui
{

namespace
{

KSharedConfig::Ptr g_config;

constexpr QLatin1StringView kGroup{"Notices"};
constexpr QLatin1StringView kHearing{"HearingAccepted"};

[[nodiscard]] KConfigGroup group()
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openConfig();
    return KConfigGroup(config, kGroup);
}

} // namespace

FirstUseNotice::FirstUseNotice(QObject *parent)
    : QObject(parent)
{
}

void FirstUseNotice::setConfig(const KSharedConfig::Ptr &config)
{
    g_config = config;
}

bool FirstUseNotice::hearingAccepted() const
{
    return group().readEntry(kHearing, false);
}

void FirstUseNotice::setHearingAccepted(bool accepted)
{
    if (accepted == hearingAccepted()) {
        return;
    }
    KConfigGroup settings = group();
    settings.writeEntry(kHearing, accepted);
    settings.sync();
    Q_EMIT hearingAcceptedChanged();
}

} // namespace rb::ui
