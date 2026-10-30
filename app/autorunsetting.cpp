// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "autorunsetting.h"

#include "core/session.h"

#include <KConfigGroup>

namespace rb::ui
{

namespace
{

KSharedConfig::Ptr g_config;

KConfigGroup group()
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openConfig();
    return KConfigGroup(config, QLatin1StringView(core::autoRunStart::group));
}

} // namespace

AutoRunSetting::AutoRunSetting(QObject *parent)
    : QObject(parent)
    , m_everyEngineStart(group().readEntry(core::autoRunStart::key, QString())
                         == QLatin1StringView(core::autoRunStart::everyEngineStart))
{
}

bool AutoRunSetting::everyEngineStart() const
{
    return m_everyEngineStart;
}

void AutoRunSetting::setEveryEngineStart(bool every)
{
    if (every == m_everyEngineStart) {
        return;
    }
    m_everyEngineStart = every;
    KConfigGroup settings = group();
    settings.writeEntry(
        core::autoRunStart::key,
        QString::fromLatin1(every ? core::autoRunStart::everyEngineStart : core::autoRunStart::onceALogin));
    settings.sync();
    Q_EMIT everyEngineStartChanged();
}

void AutoRunSetting::setConfig(const KSharedConfig::Ptr &config)
{
    g_config = config;
}

} // namespace rb::ui
