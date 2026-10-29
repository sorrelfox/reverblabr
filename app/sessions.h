// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <KSharedConfig>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <functional>
#include <memory>

namespace rb::ui
{

/// The sessions there are, the ones that are open, and which one is in front.
///
/// **What a session is belongs to the engine**; this is which of them the
/// window is holding. SessionModel follows one session's contents;
/// this follows the set of them, and it is what the tabs are drawn from.
///
/// **The open ones are remembered, so a launch is a resumption**.
/// Their ids and the order they sat in go into the state file, and on the
/// next launch they are opened again — **stopped**, because a session that
/// started making sound because the application was opened would be a
/// surprise nobody asked for. A session whose file has gone is dropped from
/// the list rather than reported: it was deleted, which is not an error.
///
/// **In the state file, not the settings file.** Which sessions were open is
/// something the application noticed, not something somebody chose, and it
/// has no business in a file a person might copy between machines.
class Sessions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Every session on disk, each `{ id, name, state, running }`, in the order the engine
    /// listed them. **What File → Open Session… offers**, including the ones
    /// that are already open: opening one that is open brings it to the
    /// front rather than opening it twice.
    Q_PROPERTY(QVariantList saved READ saved NOTIFY savedChanged)
    /// The open sessions in tab order, each `{ id, name, state, running }`.
    ///
    /// **The state is in the row** because a tab shows a live dot for its
    /// own session, and several sessions can run at once — which is the
    /// point of having tabs. AppShell only knows about the one in front.
    Q_PROPERTY(QVariantList open READ open NOTIFY openChanged)
    /// Which one is in front, or empty when none is. **Drives
    /// AppShell.sessionId**, rather than being driven by it: the frame shows
    /// whichever session this says.
    Q_PROPERTY(QString currentId READ currentId WRITE setCurrentId NOTIFY currentChanged)
    /// Whether the engine has answered with the list at least once. Until it
    /// has, an empty list means "not asked yet" and not "none".
    Q_PROPERTY(bool ready READ isReady NOTIFY savedChanged)

public:
    /// The name a session made with nothing said about it gets. Not
    /// translated here: the interface holds the words (docs/i18n.md), and
    /// this is what a test and the engine see.
    static constexpr QLatin1StringView kUntitledKey{"session.untitled"};

    explicit Sessions(QObject *parent = nullptr);
    ~Sessions() override;

    static Sessions *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    [[nodiscard]] QVariantList saved() const;
    [[nodiscard]] QVariantList open() const;
    [[nodiscard]] QString currentId() const;
    void setCurrentId(const QString &sessionId);
    [[nodiscard]] bool isReady() const;

    /// Opens again what was open when the application last closed, in the
    /// order it sat in, and brings the same one to the front.
    ///
    /// **Called once, when the window is built.** With nothing remembered —
    /// a first run, or a state file somebody removed — it makes one session
    /// so that the window is never empty. \a untitledName is what to call
    /// it, because the words are the interface's.
    Q_INVOKABLE void restore(const QString &untitledName);

    /// Makes a session, opens it and brings it to the front.
    Q_INVOKABLE void create(const QString &name);
    /// Opens a saved session, or brings it to the front if it is open.
    Q_INVOKABLE void openSession(const QString &sessionId);
    /// Closes one. The session stays on disk; only the window lets go of it.
    /// **The last one is not closed**: a window with no session has nothing
    /// to show and no way back.
    Q_INVOKABLE void closeSession(const QString &sessionId);
    Q_INVOKABLE void renameSession(const QString &sessionId, const QString &name);
    /// Deletes one, file and all. Closes it first if it is open.
    Q_INVOKABLE void deleteSession(const QString &sessionId);

    /// Moves the tab at \a from to \a to, which is what dragging one does.
    Q_INVOKABLE void moveTab(int from, int to);

    /// Writes the session in front to \a file, for sending to somebody.
    /// **A url, not a path**, because that is what a file dialog hands back.
    Q_INVOKABLE void exportCurrent(const QUrl &file);
    /// Reads a session file from anywhere, as a copy with an id of its own,
    /// and opens it in a tab.
    Q_INVOKABLE void importFrom(const QUrl &file);

    /// The extension a session file carries, so the interface's file dialogs
    /// and the engine cannot disagree about it.
    [[nodiscard]] Q_INVOKABLE static QString fileSuffix();

    /// Whether \a sessionId is one the window is holding.
    [[nodiscard]] Q_INVOKABLE bool isOpen(const QString &sessionId) const;
    /// What \a sessionId is called, from whichever of the two lists knows.
    [[nodiscard]] Q_INVOKABLE QString nameOf(const QString &sessionId) const;

    /// What \a sessionId was called when the window last wrote down what was
    /// open, or empty. **Read from the state file, not from a session**,
    /// because it is wanted exactly when the session's own file is not
    /// there to ask: a remembered session that has gone is reported
    /// by name rather than by the id it was filed under.
    [[nodiscard]] static QString rememberedName(const QString &sessionId);

    /// Writes down which sessions are open and which is in front.
    /// **Called when the window closes**, and also whenever the set changes,
    /// so that a crash still resumes to something recent.
    Q_INVOKABLE void remember();

    /// The facade to talk to. A test gives its own; the singleton takes
    /// AppShell's.
    void attach(core::EngineFacade *facade);

    /// Where to read and write. A test gives its own, so that running the
    /// tests does not reach into whoever is running them.
    static void setConfig(const KSharedConfig::Ptr &config);

Q_SIGNALS:
    void savedChanged();
    void openChanged();
    void currentChanged();
    /// A session was opened that was not open before, with its id. **For
    /// whatever has to be pointed at it** — the meters, the error surface —
    /// which are told rather than bound.
    void opened(const QString &sessionId);
    void closed(const QString &sessionId);

private:
    /// Where \a sessionId belongs in the tab order, which during a restore
    /// is where it was remembered rather than the end.
    [[nodiscard]] qsizetype tabPositionFor(const QString &sessionId) const;
    /// Every remembered session has arrived or been refused: put the front
    /// tab back, or make one if none of them came back.
    void finishRestore();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
