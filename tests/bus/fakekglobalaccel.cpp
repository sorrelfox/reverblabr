// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **Plasma's kglobalaccel, played by a test**, in a process of its own
// on the test's private bus: KGlobalAccel's calls wait for their answers,
// and a daemon in the asking thread could never give one.
//
// It keeps what registers with it — each action's name as System Settings
// would show it, its key, its default key, whether it is active — and lets
// the test read that and act as the person would, through
// org.reverblabr.Test:
//
// - Registered() → as: "component/action\tname\tactive|inactive" for every
//   action registered and not removed;
// - Removed() → as: "component/action" for every action removed with its
//   key;
// - DefaultKeyCount() → i: how many default keys were asked for, in all;
// - Press(s component, s action): the key is pressed;
// - Rebind(s component, s action, i key): the key is set in System
//   Settings, and the application told.
//
// It refuses to start on anything that looks like a desktop's session bus.
//
//   reverblabr-fake-kglobalaccel

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QStringList>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace
{

/// One key sequence as KGlobalAccel sends it: four combined keys, 0 for
/// the ones not used.
struct Keys {
    QList<int> keys;
};

QDBusArgument &operator<<(QDBusArgument &argument, const Keys &keys)
{
    argument.beginStructure();
    argument << keys.keys;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, Keys &keys)
{
    argument.beginStructure();
    argument >> keys.keys;
    argument.endStructure();
    return argument;
}

} // namespace

Q_DECLARE_METATYPE(Keys)

namespace
{

const QString kService = u"org.kde.kglobalaccel"_s;
const QString kPath = u"/kglobalaccel"_s;
const QString kInterface = u"org.kde.KGlobalAccel"_s;
const QString kComponentInterface = u"org.kde.kglobalaccel.Component"_s;
const QString kTestInterface = u"org.reverblabr.Test"_s;

/// KGlobalAccel's flags, as its private header has them.
constexpr uint kIsDefault = 0x8;

struct Action {
    QString name;
    QList<Keys> keys;
    QList<Keys> defaults;
    bool active = true;
};

[[nodiscard]] QString componentPath(const QString &component)
{
    QString path = component;
    for (QChar &c : path) {
        if (!c.isLetterOrNumber()) {
            c = u'_';
        }
    }
    return u"/component/"_s + path;
}

class Daemon : public QDBusVirtualObject
{
public:
    [[nodiscard]] QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QString member = message.member();
        const QVariantList arguments = message.arguments();
        if (message.interface() == kTestInterface) {
            return handleTest(message, connection);
        }
        if (message.path() != kPath) {
            // A component's own calls: nothing the application needs here.
            connection.send(message.createReply());
            return true;
        }
        if (member == "doRegister"_L1 && arguments.size() == 1) {
            const QStringList id = arguments.at(0).toStringList();
            if (id.size() >= 4) {
                Action &action = m_actions[keyOf(id)];
                action.name = id.at(3);
                action.active = true;
                m_removed.removeAll(keyOf(id));
            }
            connection.send(message.createReply());
            return true;
        }
        if (member == "setShortcutKeys"_L1 && arguments.size() == 3) {
            const QStringList id = arguments.at(0).toStringList();
            const auto given = qdbus_cast<QList<Keys>>(arguments.at(1));
            const uint flags = arguments.at(2).toUInt();
            Action &action = m_actions[keyOf(id)];
            action.active = true;
            if ((flags & kIsDefault) != 0) {
                action.defaults = given;
                for (const Keys &keys : given) {
                    if (!keys.keys.isEmpty() && keys.keys.constFirst() != 0) {
                        ++m_defaultKeys;
                    }
                }
                connection.send(message.createReply(QVariant::fromValue(given)));
                return true;
            }
            // **Autoloading**: a key already set wins over the one asked for,
            // which is how a key survives the application starting again.
            if (action.keys.isEmpty()) {
                action.keys = given;
            }
            connection.send(message.createReply(QVariant::fromValue(action.keys)));
            return true;
        }
        if ((member == "shortcutKeys"_L1 || member == "defaultShortcutKeys"_L1) && arguments.size() == 1) {
            const Action action = m_actions.value(keyOf(arguments.at(0).toStringList()));
            connection.send(
                message.createReply(QVariant::fromValue(member == "shortcutKeys"_L1 ? action.keys : action.defaults)));
            return true;
        }
        if (member == "setInactive"_L1 && arguments.size() == 1) {
            m_actions[keyOf(arguments.at(0).toStringList())].active = false;
            connection.send(message.createReply());
            return true;
        }
        if (member == "unRegister"_L1 && arguments.size() == 1) {
            remove(keyOf(arguments.at(0).toStringList()));
            connection.send(message.createReply());
            return true;
        }
        if (member == "unregister"_L1 && arguments.size() == 2) {
            remove(arguments.at(0).toString() + u'/' + arguments.at(1).toString());
            connection.send(message.createReply(true));
            return true;
        }
        if (member == "getComponent"_L1 && arguments.size() == 1) {
            const QString path = componentPath(arguments.at(0).toString());
            connection.send(message.createReply(QVariant::fromValue(QDBusObjectPath(path))));
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] static QString keyOf(const QStringList &id)
    {
        return id.size() >= 2 ? id.at(0) + u'/' + id.at(1) : QString{};
    }

    void remove(const QString &key)
    {
        m_actions.remove(key);
        if (!m_removed.contains(key)) {
            m_removed.append(key);
        }
    }

    bool handleTest(const QDBusMessage &message, const QDBusConnection &connection)
    {
        const QString member = message.member();
        const QVariantList arguments = message.arguments();
        if (member == "Registered"_L1) {
            QStringList registered;
            for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
                registered.append(it.key() + u'\t' + it->name + u'\t' + (it->active ? u"active"_s : u"inactive"_s));
            }
            registered.sort();
            connection.send(message.createReply(registered));
            return true;
        }
        if (member == "Removed"_L1) {
            connection.send(message.createReply(m_removed));
            return true;
        }
        if (member == "DefaultKeyCount"_L1) {
            connection.send(message.createReply(m_defaultKeys));
            return true;
        }
        if (member == "Press"_L1 && arguments.size() == 2) {
            const QString component = arguments.at(0).toString();
            QDBusMessage pressed =
                QDBusMessage::createSignal(componentPath(component), kComponentInterface, u"globalShortcutPressed"_s);
            pressed << component << arguments.at(1).toString() << QDateTime::currentMSecsSinceEpoch();
            connection.send(message.createReply());
            connection.send(pressed);
            return true;
        }
        if (member == "Rebind"_L1 && arguments.size() == 3) {
            const QString component = arguments.at(0).toString();
            const QString name = arguments.at(1).toString();
            const QList<Keys> keys{Keys{{arguments.at(2).toInt(), 0, 0, 0}}};
            m_actions[component + u'/' + name].keys = keys;
            QDBusMessage changed = QDBusMessage::createSignal(kPath, kInterface, u"yourShortcutsChanged"_s);
            changed << QStringList{component, name} << QVariant::fromValue(keys);
            connection.send(message.createReply());
            connection.send(changed);
            return true;
        }
        return false;
    }

    QHash<QString, Action> m_actions;
    QStringList m_removed;
    int m_defaultKeys = 0;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    if (address.isEmpty() || address.contains(QByteArray("/run/user/"))) {
        std::fputs("refusing to play kglobalaccel on what looks like a desktop's session bus\n", stderr);
        return 2;
    }
    qDBusRegisterMetaType<Keys>();
    qDBusRegisterMetaType<QList<Keys>>();
    Daemon daemon;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerVirtualObject(kPath, &daemon, QDBusConnection::SubPath)
        || !bus.registerVirtualObject(u"/component"_s, &daemon, QDBusConnection::SubPath)
        || !bus.registerService(kService)) {
        std::fputs("could not take kglobalaccel's name\n", stderr);
        return 1;
    }
    std::fputs("ready\n", stdout);
    std::fflush(stdout);
    return QCoreApplication::exec();
}
