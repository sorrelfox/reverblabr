// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The facade against its own interface definition. dbus/
// io.github.reverblabr.Engine1.xml is the source of truth, so a method that
// is not in it does not exist, and neither does an argument of a type D-Bus
// cannot carry. Without this the two drift apart quietly, and the day the
// engine moves onto a bus is the day everybody finds out.

#include "core/enginefacade.h"

#include <QDomDocument>
#include <QFile>
#include <QMetaMethod>
#include <QMetaObject>
#include <QSet>
#include <QString>
#include <gtest/gtest.h>

#include <map>
#include <vector>

using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

/// One method or signal, as the XML gives it.
struct Member {
    QString name;
    QStringList argumentTypes; // D-Bus signatures, in order
};

/// What a D-Bus signature means in C++, for the types this interface uses.
[[nodiscard]] QString cppTypeFor(const QString &signature)
{
    static const std::map<QString, QString> kTypes{
        {u"s"_s, u"QString"_s},
        {u"b"_s, u"bool"_s},
        {u"i"_s, u"int"_s},
        {u"u"_s, u"uint"_s},
        {u"x"_s, u"qlonglong"_s},
        {u"t"_s, u"qulonglong"_s},
        {u"d"_s, u"double"_s},
        {u"as"_s, u"QStringList"_s},
    };
    const auto found = kTypes.find(signature);
    return found == kTypes.end() ? QString{} : found->second;
}

/// `ListSessions` in the XML is `listSessions` in Qt.
[[nodiscard]] QString qtNameFor(const QString &dbusName)
{
    QString name = dbusName;
    name[0] = name[0].toLower();
    return name;
}

[[nodiscard]] std::vector<Member> membersOf(const QDomElement &interface, const QString &tag)
{
    std::vector<Member> members;
    const QDomNodeList elements = interface.elementsByTagName(tag);
    for (int index = 0; index < elements.count(); ++index) {
        const QDomElement element = elements.at(index).toElement();
        Member member;
        member.name = element.attribute(u"name"_s);
        const QDomNodeList args = element.elementsByTagName(u"arg"_s);
        for (int argument = 0; argument < args.count(); ++argument) {
            const QDomElement arg = args.at(argument).toElement();
            // A signal's arguments have no direction; a method's "out" ones
            // would be a return value, and this interface has none.
            const QString direction = arg.attribute(u"direction"_s, u"out"_s);
            EXPECT_TRUE(tag == u"signal"_s || direction == u"in"_s)
                << member.name.toStdString() << " has an out argument, and nothing here returns a value";
            member.argumentTypes.append(arg.attribute(u"type"_s));
        }
        members.push_back(std::move(member));
    }
    return members;
}

/// The interface element of the XML the engine is built against.
[[nodiscard]] QDomElement interfaceElement(QDomDocument &document)
{
    QFile file(QString::fromUtf8(RB_ENGINE_INTERFACE_XML));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << "cannot open " << RB_ENGINE_INTERFACE_XML;
    EXPECT_TRUE(document.setContent(&file).errorMessage.isEmpty());
    return document.documentElement().firstChildElement(u"interface"_s);
}

/// A stand-in that does nothing, so the interface can be reflected on.
class Silent final : public EngineFacade
{
public:
    void listSessions() override { }
    void createSession(const QString &) override { }
    void openSession(const QString &) override { }
    void saveSession(const QString &) override { }
    void closeSession(const QString &) override { }
    void deleteSession(const QString &) override { }
    void exportSession(const QString &, const QString &) override { }
    void importSession(const QString &) override { }
    void listPresets(const QString &) override { }
    void applyPreset(const QString &, const QString &, const QString &) override { }
    void savePreset(const QString &, const QString &, const QString &, bool) override { }
    void renamePreset(const QString &, const QString &, const QString &) override { }
    void deletePreset(const QString &, const QString &) override { }
    void applyEdit(const QString &, const QString &) override { }
    void setParam(const QString &, const QString &, const QString &, const QString &) override { }
    void undo(const QString &) override { }
    void redo(const QString &) override { }
    void getUndoState(const QString &) override { }
    void controlRecording(const QString &, const QString &, const QString &) override { }
    void setBroadcastPassword(const QString &, const QString &, const QString &) override { }
    void getBroadcastStatus(const QString &, const QString &) override { }
    void restartPlugin(const QString &, const QString &) override { }
    void setPluginWindow(const QString &, const QString &, bool) override { }
    void listPluginPresets(const QString &, const QString &) override { }
    void loadPluginPreset(const QString &, const QString &, const QString &) override { }
    void savePluginPreset(const QString &, const QString &, const QString &) override { }
    void analyse(const QString &, const QString &, int) override { }
    void start(const QString &) override { }
    void stop(const QString &) override { }
    void getSessionState(const QString &) override { }
    void resynchronise(const QString &) override { }
    void getInventory() override { }
    void getBlockCatalogue() override { }
    void getAudioSystem() override { }
    void reconnectAudioSystem() override { }
    void getDiagnostics() override { }
    void listVirtualDevices() override { }
    void createVirtualDevice(const QString &) override { }
    void renameVirtualDevice(const QString &, const QString &) override { }
    void setVirtualDeviceEnabled(const QString &, bool) override { }
    void deleteVirtualDevice(const QString &) override { }
    void reloadVirtualDevices() override { }
    void listRecordings(const QString &) override { }
    void rescanRecordings() override { }
    void getRecording(const QString &) override { }
    void renameRecording(const QString &, const QString &) override { }
    void setRecordingTags(const QString &, const QString &) override { }
    void previewRecording(const QString &) override { }
    void pausePreview() override { }
    void seekPreview(double) override { }
    void stopPreview() override { }
    void listPlugins() override { }
    void rescanPlugins(bool) override { }
    void describePlugin(const QString &, const QString &) override { }
    void listTimers() override { }
    void fireTimer(const QString &, bool) override { }
};

} // namespace

TEST(Facade, MatchesTheInterfaceDefinition)
{
    QDomDocument document;
    const QDomElement interface = interfaceElement(document);
    ASSERT_FALSE(interface.isNull());
    EXPECT_EQ(interface.attribute(u"name"_s), u"io.github.reverblabr.Engine1"_s);

    const Silent facade;
    const QMetaObject *meta = facade.metaObject();

    const auto check = [&](const std::vector<Member> &members, QMetaMethod::MethodType kind, const char *what) {
        for (const Member &member : members) {
            const QString wanted = qtNameFor(member.name);
            bool found = false;
            for (int index = 0; index < meta->methodCount(); ++index) {
                const QMetaMethod method = meta->method(index);
                if (method.methodType() != kind || QString::fromUtf8(method.name()) != wanted) {
                    continue;
                }
                found = true;
                ASSERT_EQ(method.parameterCount(), member.argumentTypes.size())
                    << what << " " << member.name.toStdString() << " takes a different number of arguments";
                for (qsizetype argument = 0; argument < member.argumentTypes.size(); ++argument) {
                    const QString expected = cppTypeFor(member.argumentTypes.at(argument));
                    ASSERT_FALSE(expected.isEmpty())
                        << what << " " << member.name.toStdString() << " argument "
                        << member.argumentTypes.at(argument).toStdString() << " is not a type D-Bus can carry";
                    QString actual = QString::fromUtf8(method.parameterTypeName(static_cast<int>(argument)));
                    actual.remove(u"const "_s).remove(u" &"_s);
                    EXPECT_EQ(actual, expected) << what << " " << member.name.toStdString() << " argument " << argument;
                }
                break;
            }
            EXPECT_TRUE(found) << what << " " << member.name.toStdString() << " is in the interface but not in C++";
        }
    };

    const std::vector<Member> methods = membersOf(interface, u"method"_s);
    const std::vector<Member> signals = membersOf(interface, u"signal"_s);
    EXPECT_GT(methods.size(), 10U) << "the interface looks empty; is the XML the one that was meant?";
    check(methods, QMetaMethod::Slot, "method");
    check(signals, QMetaMethod::Signal, "signal");

    // And nothing in C++ that is not in the interface, or a client reading
    // the XML would be missing something the GUI quietly relies on.
    QSet<QString> declared;
    for (const Member &member : methods) {
        declared.insert(qtNameFor(member.name));
    }
    for (const Member &member : signals) {
        declared.insert(qtNameFor(member.name));
    }
    for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (method.methodType() != QMetaMethod::Slot && method.methodType() != QMetaMethod::Signal) {
            continue;
        }
        if (method.access() != QMetaMethod::Public) {
            continue;
        }
        EXPECT_TRUE(declared.contains(QString::fromUtf8(method.name())))
            << QString::fromUtf8(method.name()).toStdString() << " is in C++ but not in the interface";
    }
}

TEST(Facade, SaysWhatAStateIsCalledOnTheWire)
{
    // The GUI switches on these, so they are worth pinning down.
    EXPECT_EQ(QString(sessionState::idle), u"idle"_s);
    EXPECT_EQ(QString(sessionState::starting), u"starting"_s);
    EXPECT_EQ(QString(sessionState::running), u"running"_s);
    EXPECT_EQ(QString(sessionState::stopping), u"stopping"_s);
    EXPECT_EQ(QString(sessionState::error), u"error"_s);
}
