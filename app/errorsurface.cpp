// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "errorsurface.h"

#include "appshell.h"
#include "core/audiosystem.h"
#include "errormessages.h"
#include "sessions.h"

#include <KLocalizedString>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <utility>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// The argument an error carries when it is about one block. **Not a
/// declared argument of any code**: it says where the error belongs, not
/// what the sentence says, so a code can grow one without its message
/// changing.
constexpr QLatin1StringView kBlockArgument{"block"};

/// The argument that says which session could not be found.
constexpr QLatin1StringView kSessionArgument{"session"};

/// **Errors about opening a session, not about the one in front**.
/// A session that could not be opened has no tab to be shown against; the
/// commonest is one a restore could not put back, and a restore ends by
/// bringing another session to the front — which, if this were cleared with
/// the rest, took the one explanation with it, or did not, depending only
/// on which of the two the engine happened to answer first.
[[nodiscard]] bool isAboutOpening(const QString &code)
{
    return code == "session.notFound"_L1 || code == "session.corruptFile"_L1;
}

/// **About the window, not about any session**: which engine it runs
/// on. It stays whichever session is in front, until it is put away.
[[nodiscard]] bool isAboutTheWindow(const QString &code)
{
    return code == "engine.daemonUnreachable"_L1;
}

/// **The Virtual Devices window's own answers**. A device that could
/// not be made or changed was asked for in that window, which says so
/// beside its list; the same sentence across the main window as well would
/// be one refusal reported twice, in a window nobody was looking at.
[[nodiscard]] bool isTheVirtualDevicesWindows(const QString &code)
{
    return code == "device.virtualNotCreated"_L1 || code == "device.virtualRefused"_L1;
}

} // namespace

class ErrorSurface::Private
{
public:
    QPointer<core::EngineFacade> facade;
    QString sessionId;

    /// The last failure that was about the session, and whether it has been
    /// put away.
    QString code;
    QString message;
    bool dismissed = false;

    /// Where the connection stands, as the engine last said.
    core::AudioSystemInfo audioSystem;
    /// What the audio system was called when it was last there. **Kept on
    /// purpose**: a server that has gone says nothing about itself, and
    /// "PipeWire isn't running" is a sentence somebody can act on where
    /// "the audio system isn't running" is one they have to translate.
    QString serverName;
    /// Whether the connection has been heard from at all. Before that,
    /// nothing is claimed: an engine that has not answered yet is not an
    /// audio system that is away.
    bool audioSystemKnown = false;

    QStringList flagged;
    QHash<QString, QString> badges;

    [[nodiscard]] bool connectionTrouble() const
    {
        return audioSystemKnown && !audioSystem.isConnected();
    }
};

ErrorSurface::ErrorSurface(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // **Attached here rather than only in create().** Whether QML reaches a
    // singleton through its create function or default-constructs it is
    // QML's business, and a singleton that only works one of those ways is
    // a singleton that works until it does not.
    attach(AppShell::engine());
    // **What the window had to say before there was a window**.
    if (const auto [code, argsJson] = AppShell::startupNotice(); !code.isEmpty()) {
        report(code, argsJson);
    }
}

ErrorSurface::~ErrorSurface() = default;

ErrorSurface *ErrorSurface::create(QQmlEngine *, QJSEngine *)
{
    // The constructor has already attached to the window's engine; this is
    // here because a QML singleton is entitled to a create function.
    return new ErrorSurface;
}

void ErrorSurface::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (facade == nullptr) {
        return;
    }

    connect(facade, &core::EngineFacade::error, this, &ErrorSurface::report);

    connect(facade, &core::EngineFacade::audioSystemChanged, this, [this](const QString &json) {
        const core::AudioSystemInfo info = core::audioSystemFromJson(json);
        const bool wasTrouble = d->connectionTrouble();
        d->audioSystem = info;
        d->audioSystemKnown = true;
        if (!info.serverName.isEmpty()) {
            d->serverName = info.serverName;
        }
        if (d->connectionTrouble() != wasTrouble) {
            // The banner is about to appear or to go; either way, a message
            // that was put away belongs to the state that has just ended.
            d->dismissed = false;
        }
        Q_EMIT changed();
    });

    facade->getAudioSystem();
}

void ErrorSurface::report(const QString &code, const QString &argsJson)
{
    if (isTheVirtualDevicesWindows(code)) {
        return;
    }
    const QJsonObject json = QJsonDocument::fromJson(argsJson.toUtf8()).object();
    QVariantMap args = json.toVariantMap();
    const QString blockId = json.value(kBlockArgument).toString();
    // **Named, not numbered.** The engine can only give the id it was
    // asked for — the file with the name in it is the thing that has
    // gone — but the window wrote the name down when it last had it.
    if (code == "session.notFound"_L1) {
        const QString name = Sessions::rememberedName(args.value(kSessionArgument).toString());
        if (!name.isEmpty()) {
            args.insert(kSessionArgument, name);
        }
    }
    const QString text = app::errorMessage(code, args);

    if (!blockId.isEmpty()) {
        // **On the tile, not at the top of the window.** An error that
        // names a block is about that block, and a badge beside the
        // thing that went wrong says more than a sentence far from it.
        if (!d->badges.contains(blockId)) {
            d->flagged.append(blockId);
        }
        d->badges.insert(blockId, text);
        Q_EMIT blocksChanged();
        return;
    }

    d->code = code;
    d->message = text;
    // A new failure is worth showing even if the last one was put away.
    d->dismissed = false;
    Q_EMIT changed();
}

QString ErrorSurface::sessionId() const
{
    return d->sessionId;
}

void ErrorSurface::setSessionId(const QString &sessionId)
{
    if (d->sessionId == sessionId) {
        return;
    }
    d->sessionId = sessionId;
    // **What went wrong in another session is not shown against this one.**
    // What went wrong opening one is about no session in particular, and
    // stays until it is put away.
    if (!isAboutOpening(d->code) && !isAboutTheWindow(d->code)) {
        d->code.clear();
        d->message.clear();
        d->dismissed = false;
    }
    const bool hadBadges = !d->flagged.isEmpty();
    d->flagged.clear();
    d->badges.clear();
    Q_EMIT sessionChanged();
    Q_EMIT changed();
    if (hadBadges) {
        Q_EMIT blocksChanged();
    }
}

bool ErrorSurface::isVisible() const
{
    if (d->dismissed) {
        return false;
    }
    return d->connectionTrouble() || !d->message.isEmpty();
}

QString ErrorSurface::message() const
{
    if (d->connectionTrouble()) {
        const QString server = d->serverName;
        if (server.isEmpty()) {
            return i18nc("@info the audio server cannot be reached",
                         "The audio system isn't running, so nothing can be played or recorded.");
        }
        return i18nc("@info the audio server cannot be reached, named",
                     "%1 isn't running, so nothing can be played or recorded.",
                     server);
    }
    return d->message;
}

QString ErrorSurface::code() const
{
    return d->connectionTrouble() ? QString{} : d->code;
}

bool ErrorSurface::canRetry() const
{
    return d->connectionTrouble();
}

bool ErrorSurface::isAboutConnection() const
{
    return d->connectionTrouble();
}

QStringList ErrorSurface::flaggedBlocks() const
{
    return d->flagged;
}

QString ErrorSurface::badge(const QString &blockId) const
{
    return d->badges.value(blockId);
}

void ErrorSurface::clearBlock(const QString &blockId)
{
    if (d->badges.remove(blockId) == 0) {
        return;
    }
    d->flagged.removeAll(blockId);
    Q_EMIT blocksChanged();
}

void ErrorSurface::dismiss()
{
    if (d->dismissed) {
        return;
    }
    d->dismissed = true;
    Q_EMIT changed();
}

void ErrorSurface::retry()
{
    if (d->facade) {
        d->facade->reconnectAudioSystem();
    }
}

} // namespace rb::ui
