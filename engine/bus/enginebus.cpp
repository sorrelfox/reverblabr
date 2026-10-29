// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "bus/enginebus.h"

#include <QDBusMetaType>
#include <QFile>
#include <QMetaType>
#include <QObject>

#include <array>

using namespace Qt::StringLiterals;

namespace rb::bus
{

QString facadeNameOf(const QString &busName)
{
    QString name = busName;
    if (!name.isEmpty()) {
        name[0] = name[0].toLower();
    }
    return name;
}

QString busNameOf(const QByteArray &facadeName)
{
    QString name = QString::fromLatin1(facadeName);
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

QString signatureOf(const QMetaMethod &method)
{
    QString signature;
    for (int index = 0; index < method.parameterCount(); ++index) {
        const char *type = QDBusMetaType::typeToSignature(method.parameterMetaType(index));
        if (type == nullptr) {
            return {};
        }
        signature += QLatin1StringView(type);
    }
    return signature;
}

bool convertArguments(const QMetaMethod &method, QVariantList &arguments)
{
    if (arguments.size() != method.parameterCount()) {
        return false;
    }
    for (int index = 0; index < method.parameterCount(); ++index) {
        const QMetaType wanted = method.parameterMetaType(index);
        QVariant &argument = arguments[index];
        // **Exactly the parameter's type**, since what is passed on is a
        // pointer to the value: a quint64 read through a pointer to a
        // qulonglong is the same thing, a qint32 read as a quint64 is not.
        if (argument.metaType() != wanted && !argument.convert(wanted)) {
            return false;
        }
    }
    return true;
}

void invoke(QObject *object, const QMetaMethod &method, QVariantList &arguments)
{
    // The array a moc-generated call hands over: the return value's place
    // first, then a pointer to each argument. Nothing here returns a value.
    std::array<void *, 11> argv{};
    for (int index = 0; index < arguments.size() && index < 10; ++index) {
        argv[static_cast<std::size_t>(index) + 1] = arguments[index].data();
    }
    QMetaObject::metacall(object, QMetaObject::InvokeMetaMethod, method.methodIndex(), argv.data());
}

namespace
{

/// The `<interface>` element of one of the XML files built into the library
/// (engine/bus/CMakeLists.txt).
[[nodiscard]] QString interfaceElement(const QString &fileName)
{
    QFile file(u":/io/github/reverblabr/bus/"_s + fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString whole = QString::fromUtf8(file.readAll());
    const qsizetype start = whole.indexOf(u"<interface"_s);
    const qsizetype end = whole.indexOf(u"</interface>"_s);
    if (start < 0 || end < 0) {
        return {};
    }
    return whole.mid(start, end - start + qsizetype{12});
}

} // namespace

QString interfaceXml()
{
    // Built into the library from the XML the facade is checked against, so
    // introspection says exactly what the conformance test holds the facade
    // to.
    return interfaceElement(u"io.github.reverblabr.Engine1.xml"_s);
}

QString telemetryInterfaceXml()
{
    return interfaceElement(u"io.github.reverblabr.Telemetry.xml"_s);
}

} // namespace rb::bus
