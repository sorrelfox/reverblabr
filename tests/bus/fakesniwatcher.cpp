// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **A system tray's StatusNotifierWatcher, played by a test**, in a
// process of its own on the test's private bus: KStatusNotifierItem asks the
// watcher its protocol version with a call that waits for the answer, and a
// watcher in the asking thread could never give one.
//
// It keeps the items that register with it, says whether a host (a tray that
// shows them) is registered, and lets the test take the host away and bring
// it back with org.reverblabr.Test.SetHostRegistered(b). It refuses to start
// on anything that looks like a desktop's session bus.
//
//   reverblabr-fake-sni-watcher [--no-host]

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QStringList>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace
{

const QString kService = u"org.kde.StatusNotifierWatcher"_s;
const QString kPath = u"/StatusNotifierWatcher"_s;
const QString kInterface = u"org.kde.StatusNotifierWatcher"_s;
const QString kProperties = u"org.freedesktop.DBus.Properties"_s;
const QString kTestInterface = u"org.reverblabr.Test"_s;

class Watcher : public QDBusVirtualObject
{
public:
    explicit Watcher(bool host)
        : m_host(host)
    {
    }

    [[nodiscard]] QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return uR"(<interface name="org.kde.StatusNotifierWatcher">
  <method name="RegisterStatusNotifierItem"><arg name="service" type="s" direction="in"/></method>
  <method name="RegisterStatusNotifierHost"><arg name="service" type="s" direction="in"/></method>
  <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
  <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
  <property name="ProtocolVersion" type="i" access="read"/>
  <signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
  <signal name="StatusNotifierItemUnregistered"><arg type="s"/></signal>
  <signal name="StatusNotifierHostRegistered"/>
  <signal name="StatusNotifierHostUnregistered"/>
</interface>)"_s;
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString member = message.member();
        if (message.interface() == kProperties) {
            if (member == "Get"_L1 && message.arguments().size() == 2) {
                const QVariant value = property(message.arguments().at(1).toString());
                if (!value.isValid()) {
                    return false;
                }
                connection.send(message.createReply(QVariant::fromValue(QDBusVariant(value))));
                return true;
            }
            if (member == "GetAll"_L1) {
                QVariantMap all;
                for (const QString &name :
                     {u"RegisteredStatusNotifierItems"_s, u"IsStatusNotifierHostRegistered"_s, u"ProtocolVersion"_s}) {
                    all.insert(name, property(name));
                }
                connection.send(message.createReply(all));
                return true;
            }
            return false;
        }
        if (message.interface() == kInterface || message.interface().isEmpty()) {
            if (member == "RegisterStatusNotifierItem"_L1 && !message.arguments().isEmpty()) {
                // **The service, or the sender when it gives only a path**,
                // as the watchers in the wild do.
                QString service = message.arguments().constFirst().toString();
                if (service.startsWith(u'/')) {
                    service = message.service();
                }
                if (!m_items.contains(service)) {
                    m_items.append(service);
                }
                connection.send(message.createReply());
                QDBusMessage registered =
                    QDBusMessage::createSignal(kPath, kInterface, u"StatusNotifierItemRegistered"_s);
                registered << service;
                connection.send(registered);
                return true;
            }
            if (member == "RegisterStatusNotifierHost"_L1) {
                connection.send(message.createReply());
                return true;
            }
            return false;
        }
        if (message.interface() == kTestInterface && member == "SetHostRegistered"_L1
            && !message.arguments().isEmpty()) {
            m_host = message.arguments().constFirst().toBool();
            connection.send(message.createReply());
            connection.send(QDBusMessage::createSignal(
                kPath, kInterface, m_host ? u"StatusNotifierHostRegistered"_s : u"StatusNotifierHostUnregistered"_s));
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] QVariant property(const QString &name) const
    {
        if (name == "RegisteredStatusNotifierItems"_L1) {
            return m_items;
        }
        if (name == "IsStatusNotifierHostRegistered"_L1) {
            return m_host;
        }
        if (name == "ProtocolVersion"_L1) {
            return 0;
        }
        return {};
    }

    bool m_host = true;
    QStringList m_items;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    if (address.isEmpty() || address.contains(QByteArray("/run/user/"))) {
        std::fputs("refusing to play a tray on what looks like a desktop's session bus\n", stderr);
        return 2;
    }
    Watcher watcher(!application.arguments().contains(u"--no-host"_s));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerVirtualObject(kPath, &watcher) || !bus.registerService(kService)) {
        std::fputs("could not take the watcher's name\n", stderr);
        return 1;
    }
    std::fputs("ready\n", stdout);
    std::fflush(stdout);
    return QCoreApplication::exec();
}
