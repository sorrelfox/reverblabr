// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "presets.h"

#include "appshell.h"
#include "core/preset.h"
#include "core/presetstore.h"

#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>

#include <array>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// The words for the presets that ship with the application.
///
/// **Their names are the interface's, not the engine's** (docs/i18n.md): a
/// factory preset travels as a stable id and is written down here so that
/// the `.pot` has it and a translator can reach it. A preset whose id is not
/// in this table is shown by its id, which is a poor name but a true one —
/// better than a blank where a later version's preset should be.
struct FactoryName {
    QLatin1StringView id;
    KLazyLocalizedString name;
};

constexpr std::array kFactoryNames{
    // Volume.
    FactoryName{"halfVolume"_L1, kli18nc("@item:inmenu a preset for the Volume block", "Half volume (50 %)")},
    FactoryName{"unityVolume"_L1, kli18nc("@item:inmenu a preset for the Volume block", "Unchanged (100 %)")},
    FactoryName{"doubleVolume"_L1, kli18nc("@item:inmenu a preset for the Volume block", "Double volume (200 %)")},
    // Dynamics Processor.
    FactoryName{"default"_L1, kli18nc("@item:inmenu a preset for the Dynamics block", "Default")},
    FactoryName{"gentleLeveling"_L1, kli18nc("@item:inmenu a preset for the Dynamics block", "Gentle leveling")},
    FactoryName{"voice"_L1, kli18nc("@item:inmenu a preset for the Dynamics block", "Voice")},
    FactoryName{"noiseGate"_L1, kli18nc("@item:inmenu a preset for the Dynamics block", "Noise gate")},
    FactoryName{"hardLimiting"_L1, kli18nc("@item:inmenu a preset for the Dynamics block", "Hard limiting")},
    // Dehum.
    FactoryName{"mains50Hz"_L1, kli18nc("@item:inmenu a preset for the Dehum block", "50 Hz mains")},
    FactoryName{"mains60Hz"_L1, kli18nc("@item:inmenu a preset for the Dehum block", "60 Hz mains")},
    // 10-Band EQ.
    FactoryName{"flat"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Flat")},
    FactoryName{"bassBoost"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Bass boost")},
    FactoryName{"lessBass"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Less bass")},
    FactoryName{"trebleBoost"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Treble boost")},
    FactoryName{"lessTreble"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Less treble")},
    FactoryName{"vocalPresence"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Vocal presence")},
    FactoryName{"podcastVoice"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Podcast voice")},
    FactoryName{"loudness"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Loudness")},
    FactoryName{"headphones"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Headphones")},
    FactoryName{"laptopSpeakers"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Laptop speakers")},
    FactoryName{"warm"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Warm")},
    FactoryName{"bright"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Bright")},
    FactoryName{"telephone"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Telephone")},
    FactoryName{"oldRadio"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Old radio")},
    FactoryName{"rumbleCut"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Rumble cut")},
    FactoryName{"hissCut"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Hiss cut")},
    FactoryName{"lessMud"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Less mud")},
    FactoryName{"lessHarshness"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Less harshness")},
    FactoryName{"air"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Air")},
    FactoryName{"smile"_L1, kli18nc("@item:inmenu a preset for the 10-Band EQ block", "Smile curve")},
    // Reverb.
    FactoryName{"smallRoom"_L1, kli18nc("@item:inmenu a preset for the Reverb block", "Small room")},
    FactoryName{"hall"_L1, kli18nc("@item:inmenu a preset for the Reverb block", "Hall")},
    FactoryName{"plate"_L1,
                kli18nc("@item:inmenu a preset for the Reverb block: a metal plate's bright, dense ring", "Plate")},
    // Hearing Compensation 16 ("flat" is the 10-Band EQ's word as well).
    FactoryName{"gentleSlope"_L1,
                kli18nc("@item:inmenu a preset for the Hearing Compensation block: more lift towards the treble",
                        "Gentle slope")},
    FactoryName{"steeperSlope"_L1,
                kli18nc("@item:inmenu a preset for the Hearing Compensation block: more lift towards the treble",
                        "Steeper slope")},
};

[[nodiscard]] QString factoryName(const QString &id)
{
    for (const FactoryName &known : kFactoryNames) {
        if (id == known.id) {
            KLocalizedString localized = known.name;
#ifdef TRANSLATION_DOMAIN
            localized = localized.withDomain(TRANSLATION_DOMAIN);
#endif
            return localized.toString();
        }
    }
    return id;
}

/// One preset as the engine sent it.
struct Known {
    QString id;
    QString name;
    bool factory = false;
    QJsonObject params;
};

} // namespace

class Presets::Private
{
public:
    QPointer<core::EngineFacade> facade;
    /// What the engine last said, by block type.
    QHash<QString, QList<Known>> byType;
    /// The types already asked about, so that a binding reading an empty
    /// list does not ask again on every frame.
    QSet<QString> asked;
    int revision = 0;
    /// Where a user preset's file is. Only the interface needs this, and
    /// only to show it in a file manager.
    core::PresetStore store;

    [[nodiscard]] const Known *find(const QString &typeId, const QString &presetId) const
    {
        const auto known = byType.constFind(typeId);
        if (known == byType.constEnd()) {
            return nullptr;
        }
        for (const Known &preset : *known) {
            if (preset.id == presetId) {
                return &preset;
            }
        }
        return nullptr;
    }
};

Presets::Presets(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // Attached here rather than only in create(), because attaching AppShell
    // only there once left Run doing nothing: whether QML reaches a singleton
    // through its create function is QML's business, and a singleton that
    // only works one of those ways works until it does not.
    attach(AppShell::engine());
}

Presets::~Presets() = default;

Presets *Presets::create(QQmlEngine *, QJSEngine *)
{
    return new Presets;
}

void Presets::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    d->byType.clear();
    d->asked.clear();
    if (!d->facade) {
        return;
    }
    connect(d->facade, &core::EngineFacade::presetsListed, this, [this](const QString &typeId, const QString &json) {
        QList<Known> presets;
        const QJsonArray listed = QJsonDocument::fromJson(json.toUtf8()).array();
        for (const QJsonValue &entry : listed) {
            const QJsonObject object = entry.toObject();
            Known preset;
            preset.id = object.value(u"id"_s).toString();
            preset.factory = object.value(u"factory"_s).toBool();
            // **The words for a shipped preset are ours.** The engine sends
            // no name for one, because it has no idea what language anybody
            // speaks.
            preset.name = preset.factory ? factoryName(preset.id) : object.value(u"name"_s).toString();
            preset.params = object.value(u"params"_s).toObject();
            if (!preset.id.isEmpty()) {
                presets.append(preset);
            }
        }
        d->byType.insert(typeId, presets);
        ++d->revision;
        Q_EMIT revisionChanged();
        Q_EMIT changed(typeId);
    });

    // **What the footer listens to instead of the error banner.** Saving
    // onto an existing preset is refused on purpose, so that the dialog can
    // say what replacing it would do rather than doing it unasked.
    connect(d->facade, &core::EngineFacade::error, this, [this](const QString &code, const QString &argsJson) {
        if (code != "preset.refused"_L1) {
            return;
        }
        const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
        Q_EMIT refused(args.value(u"preset"_s).toString(), args.value(u"reason"_s).toString());
    });
}

int Presets::revision() const
{
    return d->revision;
}

QVariantList Presets::forType(const QString &typeId)
{
    if (!typeId.isEmpty() && !d->asked.contains(typeId)) {
        d->asked.insert(typeId);
        // **Asked after this returns, never during it**. This is
        // called from a binding, and an engine in the same process answers
        // at once: the answer bumped `revision` while the binding that
        // reads it was still being worked out, which QML reports as a
        // binding loop on every first panel of a kind. Queued, the binding
        // finishes with the empty list and is worked out again when the
        // answer comes, as it would be from an engine over D-Bus.
        if (d->facade) {
            QMetaObject::invokeMethod(
                d->facade, [facade = d->facade, typeId] { facade->listPresets(typeId); }, Qt::QueuedConnection);
        }
    }
    QVariantList rows;
    for (const Known &preset : d->byType.value(typeId)) {
        rows.append(QVariantMap{
            {u"id"_s, preset.id},
            {u"name"_s, preset.name},
            {u"factory"_s, preset.factory},
        });
    }
    return rows;
}

void Presets::refresh(const QString &typeId)
{
    if (d->facade && !typeId.isEmpty()) {
        d->facade->listPresets(typeId);
    }
}

QString Presets::shownBy(const QString &typeId, const QString &marker, const QVariantMap &params) const
{
    const QList<Known> presets = d->byType.value(typeId);
    if (presets.isEmpty()) {
        return {};
    }
    // As JSON, so that a preset's values and a block's are compared without
    // either side guessing at the other's types.
    QJsonObject current;
    for (auto value = params.constBegin(); value != params.constEnd(); ++value) {
        current.insert(value.key(), QJsonValue::fromVariant(value.value()));
    }

    const auto matches = [&current](const Known &known) {
        core::Preset preset;
        preset.params = known.params;
        return core::presetMatches(preset, current);
    };

    // **The marker first, if it still holds.** A block set from a preset and
    // not touched since shows that one; only when the values have moved does
    // the answer change.
    if (const Known *said = d->find(typeId, marker); said != nullptr && matches(*said)) {
        return marker;
    }
    // Otherwise whichever preset the values are, if any: a block put back to
    // exactly what a preset says is showing it, whatever it was last set
    // from.
    for (const Known &known : presets) {
        if (matches(known)) {
            return known.id;
        }
    }
    return {};
}

QString Presets::nameOf(const QString &typeId, const QString &presetId) const
{
    const Known *preset = d->find(typeId, presetId);
    return preset == nullptr ? QString{} : preset->name;
}

bool Presets::isFactory(const QString &typeId, const QString &presetId) const
{
    const Known *preset = d->find(typeId, presetId);
    return preset != nullptr && preset->factory;
}

QUrl Presets::fileOf(const QString &typeId, const QString &presetId) const
{
    if (isFactory(typeId, presetId) || d->find(typeId, presetId) == nullptr) {
        return {}; // a shipped preset is in the binary; there is no file
    }
    return QUrl::fromLocalFile(d->store.pathFor(typeId, presetId));
}

void Presets::apply(const QString &sessionId, const QString &blockId, const QString &presetId)
{
    if (!d->facade || sessionId.isEmpty() || blockId.isEmpty() || presetId.isEmpty()) {
        return;
    }
    d->facade->applyPreset(sessionId, blockId, presetId);
}

void Presets::saveAs(const QString &sessionId, const QString &blockId, const QString &name, bool replace)
{
    if (!d->facade || sessionId.isEmpty() || blockId.isEmpty()) {
        return;
    }
    d->facade->savePreset(sessionId, blockId, name, replace);
}

void Presets::rename(const QString &typeId, const QString &presetId, const QString &name)
{
    if (d->facade) {
        d->facade->renamePreset(typeId, presetId, name);
    }
}

void Presets::remove(const QString &typeId, const QString &presetId)
{
    if (d->facade) {
        d->facade->deletePreset(typeId, presetId);
    }
}

} // namespace rb::ui
