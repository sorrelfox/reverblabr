// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

#include <memory>

namespace rb::ui
{

/// Everything that has gone wrong, in the two places a person looks.
///
/// **The engine reports codes; this is where they become sentences.** An
/// error arrives as `{code, args}` and never as text ([ADR-0003]), because
/// the engine has no idea what language anybody speaks. The message table
/// holds the words, and this decides where they go.
///
/// **Two places, by what the error is about.** An error that names a block
/// belongs on that block's tile, where the thing that went wrong is: a badge
/// beside the device that was unplugged says more than a sentence at the top
/// of the window. Everything else is about the session, and goes in the
/// banner.
///
/// **The audio system being away outranks all of it.** Every other failure
/// while PipeWire is not running is a consequence of that one, and a person
/// told four things is told nothing; so the banner says that, and offers the
/// one action that can help.
class ErrorSurface : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The session the errors are about. Set by the window; changing it
    /// clears everything, because an error about the last session is not
    /// something to show against this one.
    Q_PROPERTY(QString sessionId READ sessionId WRITE setSessionId NOTIFY sessionChanged)

    /// Whether the banner has anything to say.
    Q_PROPERTY(bool visible READ isVisible NOTIFY changed)
    /// What it says, translated and ready to read.
    Q_PROPERTY(QString message READ message NOTIFY changed)
    /// The engine's code for it, for a test and for the log. Empty when the
    /// banner is about the connection rather than about one failure.
    Q_PROPERTY(QString code READ code NOTIFY changed)
    /// Whether there is anything worth trying again, which there is exactly
    /// when the audio system is what is missing.
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    /// Whether what is showing is the audio system being away, which the
    /// banner draws as a warning rather than as a failure: nothing is
    /// broken, something is missing.
    Q_PROPERTY(bool aboutConnection READ isAboutConnection NOTIFY changed)

    /// The blocks wearing a badge, in the order they first got one. **What
    /// the canvas reads** to put a mark on a tile.
    Q_PROPERTY(QStringList flaggedBlocks READ flaggedBlocks NOTIFY blocksChanged)

public:
    explicit ErrorSurface(QObject *parent = nullptr);
    ~ErrorSurface() override;

    static ErrorSurface *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade: its errors and where its connection stands.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] QString sessionId() const;
    void setSessionId(const QString &sessionId);

    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] QString code() const;
    [[nodiscard]] bool canRetry() const;
    [[nodiscard]] bool isAboutConnection() const;
    [[nodiscard]] QStringList flaggedBlocks() const;

    /// What one block's badge says, or empty when it has none.
    [[nodiscard]] Q_INVOKABLE QString badge(const QString &blockId) const;
    /// Takes a block's badge off — what looking at the tile means.
    Q_INVOKABLE void clearBlock(const QString &blockId);

    /// Puts the banner away. It comes back when something else goes wrong,
    /// or when the connection changes: **dismissing a message is not
    /// mending what it was about.**
    Q_INVOKABLE void dismiss();
    /// Asks the engine to reach the audio system again.
    Q_INVOKABLE void retry();

Q_SIGNALS:
    void sessionChanged();
    void changed();
    void blocksChanged();

private:
    /// An error, \a code with \a argsJson, put where it belongs: on a
    /// block's tile or in the banner.
    void report(const QString &code, const QString &argsJson);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
