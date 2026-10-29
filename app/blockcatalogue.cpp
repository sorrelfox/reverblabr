// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blockcatalogue.h"

#include "appshell.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace rb::ui
{

class BlockCatalogue::Private
{
public:
    QPointer<core::EngineFacade> facade;
    bool ready = false;
    QStringList types;
    /// By type id, so that a panel asking about one block does not walk the
    /// whole catalogue.
    QHash<QString, QVariantMap> blocks;
};

BlockCatalogue::BlockCatalogue(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // **Attached here rather than only in create().** Whether QML reaches a
    // singleton through its create function or default-constructs it is
    // Qt's business and has changed before; a catalogue that quietly has no
    // engine is a panel that quietly has no parameters.
    attach(AppShell::engine());
}

BlockCatalogue::~BlockCatalogue() = default;

BlockCatalogue *BlockCatalogue::create(QQmlEngine *, QJSEngine *)
{
    // The constructor has already attached to the window's engine; this is
    // here because a QML singleton is entitled to a create function, and a
    // reader looking for one should find it.
    return new BlockCatalogue;
}

void BlockCatalogue::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade == nullptr) {
        return;
    }
    connect(facade, &core::EngineFacade::blockCatalogueChanged, this, [this](const QString &catalogueJson) {
        setCatalogue(QJsonDocument::fromJson(catalogueJson.toUtf8()).object());
    });
    // **Asked for once.** It cannot change while the engine runs: a build
    // has the blocks it was compiled with.
    facade->getBlockCatalogue();
}

void BlockCatalogue::setCatalogue(const QJsonObject &catalogue)
{
    d->types.clear();
    d->blocks.clear();
    for (const QJsonValue &entry : catalogue.value("blocks"_L1).toArray()) {
        const QJsonObject block = entry.toObject();
        const QString typeId = block.value("type"_L1).toString();
        if (typeId.isEmpty()) {
            continue;
        }
        d->types.append(typeId);
        d->blocks.insert(typeId, block.toVariantMap());
    }
    d->ready = true;
    Q_EMIT changed();
}

bool BlockCatalogue::isReady() const
{
    return d->ready;
}

QStringList BlockCatalogue::types() const
{
    return d->types;
}

QVariantMap BlockCatalogue::describe(const QString &typeId) const
{
    return d->blocks.value(typeId);
}

QVariantList BlockCatalogue::params(const QString &typeId) const
{
    return d->blocks.value(typeId).value("params"_L1).toList();
}

QVariantMap BlockCatalogue::describeParam(const QString &typeId, const QString &paramId) const
{
    for (const QVariant &entry : params(typeId)) {
        const QVariantMap param = entry.toMap();
        if (param.value("id"_L1).toString() == paramId) {
            return param;
        }
    }
    return {};
}

} // namespace rb::ui
