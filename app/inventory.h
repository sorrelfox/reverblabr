// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// What the machine can play to and record from, as the pickers read it.
///
/// **One list for the whole window.** The Output Device panel, the Input
/// Device panel and every face that names a device are all looking at the
/// same graph; three objects following the engine separately would be three
/// chances to disagree about which devices are there — and the whole point
/// of the lists is that they are live.
///
/// **It classifies nothing.** Which nodes are inputs and which are outputs
/// is the engine's business, and it has the ports and the media classes to
/// decide it with; this keeps what it was told, in the order it was told it,
/// and answers questions about it quickly.
///
/// **Applications are named by a match rule, never by an id.** A stream's
/// node id lasts as long as the stream does, and a session file outlives
/// both; what identifies an application here is the rule the engine works
/// out (`binary:firefox`, `flatpak:org.videolan.VLC`), which is what an
/// exclusion list holds and what lets somebody leave out a program that is
/// not running.
class Inventory : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether the engine has answered yet. A picker opened before it has
    /// offers nothing rather than claiming there are no devices.
    Q_PROPERTY(bool ready READ isReady NOTIFY changed)
    /// Counts up once per inventory. **What a binding watches**: the lists
    /// are read through invokable methods, and a method call is not a
    /// dependency a binding can notice on its own.
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    /// Everything that can be played to, and everything that can be recorded
    /// from, each as `{node, description, nick, mediaClass, channels,
    /// channelCount, default, virtual}`.
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)
    /// The node names the system has actually made the defaults, which is
    /// what "Default output" and "Default input" resolve to. Empty when
    /// there is nothing to be the default.
    Q_PROPERTY(QString defaultOutput READ defaultOutput NOTIFY changed)
    Q_PROPERTY(QString defaultInput READ defaultInput NOTIFY changed)
    /// Every application with a stream of its own, each as `{key, name,
    /// icon, binary, flatpakId, streamCount, sinks}`. `key` is the match
    /// rule; the rest is what a picker draws.
    Q_PROPERTY(QVariantList applications READ applications NOTIFY changed)
    /// The device reference that means "whatever the system default is"
    /// rather than naming a node. **The blocks' own spelling of it**, which
    /// is what a session file holds.
    Q_PROPERTY(QString defaultDevice READ defaultDevice CONSTANT)

public:
    /// What a device parameter holds when the block follows the system
    /// default. It is the engine's constant (`OutputDevice::kDefaultDevice`);
    /// the interface may not reach into the block layer, so the one word is
    /// written here as well and a test holds the two together.
    static constexpr QLatin1StringView kDefaultDevice{"default"};

    explicit Inventory(QObject *parent = nullptr);
    ~Inventory() override;

    static Inventory *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade, asking it for the inventory at once. A test gives
    /// its own; the QML singleton is given the window's.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] qulonglong revision() const;
    [[nodiscard]] QVariantList outputs() const;
    [[nodiscard]] QVariantList inputs() const;
    [[nodiscard]] QVariantList applications() const;
    [[nodiscard]] QString defaultOutput() const;
    [[nodiscard]] QString defaultInput() const;
    [[nodiscard]] QString defaultDevice() const;

    /// One application by its match rule, or an empty map.
    [[nodiscard]] Q_INVOKABLE QVariantMap application(const QString &key) const;

    /// Whether anything matching the rule is playing at the moment. **What
    /// an exclusion for a program nobody has opened looks like**: it is
    /// kept and shown, because the rule is the point — an application has
    /// to be excludable before it starts, or the exclusion arrives too
    /// late.
    [[nodiscard]] Q_INVOKABLE bool hasApplication(const QString &key) const;

    /// What to call the application in a list: what it calls itself while
    /// it is playing, and the readable half of the rule when it is not.
    [[nodiscard]] Q_INVOKABLE QString applicationName(const QString &key) const;

    /// The match rule for something somebody typed. A rule pasted in is
    /// taken as it stands; anything else is the name of a program, which
    /// is what `binary:` means.
    [[nodiscard]] Q_INVOKABLE QString applicationRule(const QString &text) const;

    /// The one rule a block's three identity fields come to, in the order
    /// the engine matches them: the binary, then the Flatpak id, then the
    /// name. Empty when the block has not been pointed at anything.
    ///
    /// **The session stores three fields and a picker deals in one rule**,
    /// because a program installed one way today and another way tomorrow
    /// is the same program; the two spellings of that are turned into one
    /// another here rather than in each panel that needs it.
    [[nodiscard]] Q_INVOKABLE QString applicationRuleFrom(const QString &binary,
                                                          const QString &flatpakId,
                                                          const QString &name) const;

    /// The other direction: `{binary, flatpakId, name}` for a rule, with
    /// the two the rule does not name left empty — which is what has to be
    /// written, so that a block pointed at something new stops matching
    /// what it was pointed at before.
    ///
    /// **A node-scoped rule names none of the three** and comes back as
    /// three empty fields: a node is one stream on one boot, and a session
    /// file outlives both.
    [[nodiscard]] Q_INVOKABLE QVariantMap applicationFields(const QString &rule) const;

    /// One device by `node.name`, or an empty map. kDefaultDevice asks
    /// about whatever the default is at the moment.
    [[nodiscard]] Q_INVOKABLE QVariantMap output(const QString &node) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap input(const QString &node) const;

    /// Whether the device is in the list at the moment. **What an unplugged
    /// headset looks like** to a panel when nothing is running and there is
    /// no telemetry to ask.
    [[nodiscard]] Q_INVOKABLE bool hasOutput(const QString &node) const;
    [[nodiscard]] Q_INVOKABLE bool hasInput(const QString &node) const;

    /// What to call the device in a picker: its description, or the node
    /// name when the inventory has never heard of it — which is what a
    /// session that names a device nobody has plugged in yet looks like.
    [[nodiscard]] Q_INVOKABLE QString outputName(const QString &node) const;
    [[nodiscard]] Q_INVOKABLE QString inputName(const QString &node) const;

    /// The device's own channels, by the names its ports carry: `FL`, `FR`,
    /// `AUX3`. Empty for a device that is not there.
    [[nodiscard]] Q_INVOKABLE QStringList outputChannels(const QString &node) const;
    [[nodiscard]] Q_INVOKABLE QStringList inputChannels(const QString &node) const;

    /// Reads an inventory that did not come from the engine. **For tests**,
    /// so that a picker can be checked against devices no machine has.
    void setInventory(const QJsonObject &inventory);

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
