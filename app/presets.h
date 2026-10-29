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
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// The presets a block can be set from, as the panel footer draws them.
///
/// **What a preset is belongs to the engine**; this is which of them there
/// are for a kind of block, and which one a block is showing. The values
/// never travel through here on their way to a block: the panel asks for a
/// preset by name and the engine reads it, so the interface cannot apply
/// half of one.
///
/// **"Manual" is worked out, not remembered.** A block carries the id of the
/// preset it was last set from, but nothing clears that when somebody turns
/// a knob — and it should not, because undo has to be able to put it back.
/// So the footer asks this whether the block's values are still a preset's,
/// and a block one knob away from "Voice" reads as Manual whatever it says
/// it was set from. That is what is required, and it is also right
/// for a block whose values happen to equal a preset nobody applied.
class Presets : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Counts up whenever the presets for any type change. **What a binding
    /// reads** so that a list redraws when one is saved or deleted: a
    /// QVariantList returned from a function is not something QML can watch.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit Presets(QObject *parent = nullptr);
    ~Presets() override;

    static Presets *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Every preset for \a typeId, each `{ id, name, factory }`, shipped
    /// ones first. **Asks the engine the first time it is called for a
    /// type** and answers from what it was told after that, because a
    /// binding reads a property far more often than a directory changes.
    [[nodiscard]] Q_INVOKABLE QVariantList forType(const QString &typeId);

    /// Asks the engine again, for when something has been saved or deleted.
    Q_INVOKABLE void refresh(const QString &typeId);

    /// The id of the preset these values are, or empty for none — which is
    /// what the footer calls "Manual".
    ///
    /// **The caller passes the values in.** This object talks to the engine;
    /// what a block is set to now belongs to the session model, and a
    /// singleton that quietly depended on somebody having bound it to one
    /// would work exactly until somebody forgot.
    ///
    /// \a marker is the preset id the block carries, which wins when the
    /// values are still its; otherwise the first preset the values are.
    [[nodiscard]] Q_INVOKABLE QString shownBy(const QString &typeId,
                                              const QString &marker,
                                              const QVariantMap &params) const;

    /// What to call \a presetId of \a typeId. **A factory preset's words are
    /// the interface's**, looked up from its id; a person's own preset is
    /// called what they called it.
    [[nodiscard]] Q_INVOKABLE QString nameOf(const QString &typeId, const QString &presetId) const;

    /// Whether \a presetId of \a typeId is one that ships with the
    /// application, and so cannot be renamed, deleted or saved over.
    [[nodiscard]] Q_INVOKABLE bool isFactory(const QString &typeId, const QString &presetId) const;

    /// Where a preset of the person's own is kept, for showing it in a file
    /// manager. Empty for a factory preset, which has no file.
    [[nodiscard]] Q_INVOKABLE QUrl fileOf(const QString &typeId, const QString &presetId) const;

    Q_INVOKABLE void apply(const QString &sessionId, const QString &blockId, const QString &presetId);
    Q_INVOKABLE void
    saveAs(const QString &sessionId, const QString &blockId, const QString &name, bool replace = false);
    Q_INVOKABLE void rename(const QString &typeId, const QString &presetId, const QString &name);
    Q_INVOKABLE void remove(const QString &typeId, const QString &presetId);

    [[nodiscard]] int revision() const;

    /// The facade to talk to. A test gives its own; the singleton takes
    /// AppShell's.
    void attach(core::EngineFacade *facade);

Q_SIGNALS:
    void revisionChanged();
    /// A preset could not be saved, renamed or deleted, with the engine's
    /// own word for why. **The footer needs this and not the error banner**:
    /// saving onto an existing preset is refused on purpose, so that the
    /// dialog can say what replacing it would do and offer it again.
    void refused(const QString &presetId, const QString &reason);
    /// The presets for a type are not what they were: one was saved, renamed
    /// or deleted. **What a footer redraws on.**
    void changed(const QString &typeId);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
