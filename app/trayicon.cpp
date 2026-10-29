// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "trayicon.h"

#include "traystate.h"

#include <KLocalizedString>
#include <KStatusNotifierItem>
#include <KWindowSystem>
#include <QAction>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QMenu>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

const QString kWatcherService = u"org.kde.StatusNotifierWatcher"_s;
const QString kWatcherPath = u"/StatusNotifierWatcher"_s;
const QString kWatcherInterface = u"org.kde.StatusNotifierWatcher"_s;

/// **A click opens the popover, not the window.** KStatusNotifierItem's own
/// activate() toggles its associated window; this item has none, and a left
/// click is the popover's.
class Item : public KStatusNotifierItem
{
public:
    Item(TrayState *state, QObject *parent)
        : KStatusNotifierItem(u"reverblabr"_s, parent)
        , m_state(state)
    {
    }

    void activate(const QPoint &pos) override
    {
        Q_UNUSED(pos)
        passActivationToken();
        m_state->requestPopover();
    }

    /// **Wayland lets a window come forward only with a token**, which the
    /// tray hands over with the click; the next window asked to activate
    /// uses it.
    void passActivationToken() const
    {
        const QString token = providedToken();
        if (!token.isEmpty()) {
            KWindowSystem::setCurrentXdgActivationToken(token);
        }
    }

private:
    TrayState *m_state;
};

} // namespace

class TrayIcon::Private
{
public:
    TrayState *state = nullptr;
    Item *item = nullptr;
    QDBusServiceWatcher watcher;

    void show() const
    {
        if (state->needsAttention()) {
            item->setStatus(KStatusNotifierItem::NeedsAttention);
        } else if (state->status() == TrayState::kActive) {
            item->setStatus(KStatusNotifierItem::Active);
        } else {
            item->setStatus(KStatusNotifierItem::Passive);
        }
        item->setOverlayIconByName(state->isRecording() ? QString(kRecordingOverlay) : QString());
        item->setToolTipSubTitle(state->toolTip());
    }

    void updateAvailable() const
    {
        state->setAvailable(hostAvailable(QDBusConnection::sessionBus()));
    }
};

TrayIcon::TrayIcon(TrayState *state, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->state = state;
    d->item = new Item(state, this);
    d->item->setCategory(KStatusNotifierItem::ApplicationStatus);
    d->item->setTitle(i18nc("@title the application's name in the system tray", "Reverblabr"));
    // **By name once installed**, as the launcher shows it; before that the
    // picture itself, since a tray looks a name up in its own theme, where a
    // build that has not been installed has nothing. An item carries one or
    // the other: setting a picture clears the name.
    if (applicationIconInstalled()) {
        d->item->setIconByName(kIconName);
        d->item->setToolTipIconByName(kIconName);
    } else {
        d->item->setIconByPixmap(applicationIcon());
        d->item->setToolTipIconByPixmap(applicationIcon());
    }
    d->item->setAttentionIconByName(kAttentionIcon);
    d->item->setToolTipTitle(i18nc("@title the application's name in the system tray", "Reverblabr"));

    connect(d->item, &KStatusNotifierItem::secondaryActivateRequested, this, [this] {
        static_cast<Item *>(d->item)->passActivationToken();
        d->state->requestWindowToggle();
    });

    // Above the standard Quit, which KStatusNotifierItem puts last.
    if (QMenu *menu = d->item->contextMenu(); menu != nullptr) {
        QAction *first = menu->actions().value(0, nullptr);
        auto *show =
            new QAction(applicationIcon(), i18nc("@action:inmenu in the system tray menu", "Show Reverblabr"), menu);
        connect(show, &QAction::triggered, this, [this] { d->state->requestWindowToggle(); });
        auto *sessions = new QAction(QIcon::fromTheme(u"view-list-details"_s),
                                     i18nc("@action:inmenu in the system tray menu", "All Sessions…"),
                                     menu);
        connect(sessions, &QAction::triggered, this, [this] { d->state->requestSessionsWindow(); });
        menu->insertAction(first, show);
        menu->insertAction(first, sessions);
        if (first != nullptr) {
            menu->insertSeparator(first);
        }
    }

    connect(state, &TrayState::changed, this, [this] { d->show(); });
    d->show();

    // **Whether there is a tray at all**, now and as hosts come and go: a
    // Plasma restart takes the host away for a moment and brings it back.
    d->watcher.setConnection(QDBusConnection::sessionBus());
    d->watcher.addWatchedService(kWatcherService);
    d->watcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    connect(&d->watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] { d->updateAvailable(); });
    // The watcher's own word when a host comes or goes; a D-Bus signal needs
    // a slot rather than a lambda.
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const QString &member : {u"StatusNotifierHostRegistered"_s, u"StatusNotifierHostUnregistered"_s}) {
        bus.connect(kWatcherService, kWatcherPath, kWatcherInterface, member, this, SLOT(hostsChanged()));
    }
    d->updateAvailable();
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::hostsChanged()
{
    d->updateAvailable();
}

bool TrayIcon::hostAvailable(const QDBusConnection &bus)
{
    if (!bus.isConnected() || bus.interface() == nullptr || !bus.interface()->isServiceRegistered(kWatcherService)) {
        return false;
    }
    QDBusMessage ask =
        QDBusMessage::createMethodCall(kWatcherService, kWatcherPath, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    ask << kWatcherInterface << u"IsStatusNotifierHostRegistered"_s;
    const QDBusMessage answer = bus.call(ask, QDBus::Block, 1000);
    if (answer.type() != QDBusMessage::ReplyMessage || answer.arguments().isEmpty()) {
        return false;
    }
    return answer.arguments().constFirst().value<QDBusVariant>().variant().toBool();
}

KStatusNotifierItem *TrayIcon::item() const
{
    return d->item;
}

} // namespace rb::ui
