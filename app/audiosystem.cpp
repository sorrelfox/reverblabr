// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "audiosystem.h"

#include "appshell.h"

#include <QPointer>

namespace rb::ui
{

class AudioSystem::Private
{
public:
    QPointer<core::EngineFacade> facade;
    core::AudioSystemInfo info;
};

AudioSystem::AudioSystem(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // **Attached here rather than only in create().** Whether QML reaches a
    // singleton through its create function or default-constructs it is
    // QML's business, and a singleton that only works one of those ways is
    // a singleton that works until it does not.
    attach(AppShell::engine());
}

AudioSystem::~AudioSystem() = default;

AudioSystem *AudioSystem::create(QQmlEngine *, QJSEngine *)
{
    // The constructor has already attached to the window's engine; this is
    // here because a QML singleton is entitled to a create function.
    return new AudioSystem;
}

void AudioSystem::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade != nullptr) {
        connect(facade, &core::EngineFacade::audioSystemChanged, this, [this](const QString &json) {
            const core::AudioSystemInfo info = core::audioSystemFromJson(json);
            if (info == d->info) {
                return;
            }
            d->info = info;
            Q_EMIT changed();
        });
        // **What it is running on now**, rather than what it will be when
        // something next changes: a window that opens while the audio
        // system is away has to say so straight away.
        facade->getAudioSystem();
    } else {
        d->info = core::AudioSystemInfo{};
        Q_EMIT changed();
    }
}

bool AudioSystem::isConnected() const
{
    return d->info.isConnected();
}

QString AudioSystem::state() const
{
    return d->info.state;
}

QString AudioSystem::serverName() const
{
    return d->info.serverName;
}

QString AudioSystem::serverVersion() const
{
    return d->info.serverVersion;
}

QString AudioSystem::sessionManagerName() const
{
    return d->info.sessionManagerName;
}

QString AudioSystem::sessionManagerVersion() const
{
    return d->info.sessionManagerVersion;
}

QString AudioSystem::driver() const
{
    return d->info.driver;
}

int AudioSystem::rate() const
{
    return d->info.rate;
}

int AudioSystem::quantum() const
{
    return d->info.quantum;
}

qreal AudioSystem::cycleMs() const
{
    if (d->info.rate <= 0 || d->info.quantum <= 0) {
        return 0.0;
    }
    return 1000.0 * d->info.quantum / d->info.rate; // NOLINT(*-magic-numbers)
}

void AudioSystem::reconnect()
{
    if (d->facade) {
        d->facade->reconnectAudioSystem();
    }
}

} // namespace rb::ui
