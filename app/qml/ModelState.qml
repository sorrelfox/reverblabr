// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// What a Model block is doing, worked out once for its face and its panel.
//
// **Sound only leaves this block after the model has processed it.** So
// every state but Running is silent, and the card says why, in plain words.
// The words are here, and the numbers they stand for are the block's
// (ModelBlock::State, model::Refusal, model::Family, model::Device).
QtObject {
    id: reading

    /// The block this is about.
    required property string blockId
    /// Whether the block is switched on, by the tile's switch.
    property bool blockEnabled: true

    /// Which of the block's published values is which.
    readonly property int stateValue: 0
    readonly property int latencyValue: 1
    readonly property int loadValue: 2
    readonly property int detailValue: 3

    /// The states, as ModelBlock::State numbers them.
    readonly property int noModel: 0
    readonly property int loading: 1
    readonly property int running: 2
    readonly property int cannotKeepUp: 3
    readonly property int failed: 4
    readonly property int restarting: 5
    readonly property int badOutput: 6
    /// The refusal that says the session's cycle is too long for the spare time.
    readonly property int cycleTooLong: 16

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(reading.blockId) : ({})
    /// The folder as the session holds it.
    readonly property string folder: reading.params.folder ?? ""
    readonly property string folderName: reading.folder.substring(reading.folder.lastIndexOf("/") + 1)
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(reading.blockId) : ({})
    readonly property var values: reading.levels.values ?? []
    /// Whether the running block is publishing.
    readonly property bool live: Meters.live && reading.values.length > reading.detailValue

    readonly property int state: reading.live ? Math.round(reading.values[reading.stateValue]) : -1
    readonly property real latencyMs: reading.live ? reading.values[reading.latencyValue] : 0
    readonly property real load: reading.live ? reading.values[reading.loadValue] : 0
    readonly property int detail: reading.live ? Math.round(reading.values[reading.detailValue]) : 0

    /// Whether sound is leaving the block now.
    readonly property bool sounding: reading.blockEnabled && reading.state === reading.running
    /// Whether something is wrong enough to mark.
    readonly property bool troubled: reading.blockEnabled
        && (reading.state === reading.failed || reading.state === reading.cannotKeepUp
            || reading.state === reading.badOutput)

    /// The state in a word or two, for the tile.
    readonly property string stateWord: {
        if (!reading.blockEnabled) {
            return KI18n.i18nc("@info a Model block switched off", "Off");
        }
        if (reading.folder === "" || reading.state === reading.noModel) {
            return KI18n.i18nc("@info a Model block with no folder chosen", "No model");
        }
        switch (reading.state) {
        case reading.loading:
            return KI18n.i18nc("@info a Model block whose model is loading", "Loading");
        case reading.running:
            return KI18n.i18nc("@info a Model block processing sound", "Running");
        case reading.cannotKeepUp:
            return KI18n.i18nc("@info a Model block whose model is too slow", "Cannot keep up");
        case reading.failed:
            return KI18n.i18nc("@info a Model block whose model will not run", "Failed");
        case reading.restarting:
            return KI18n.i18nc("@info a Model block whose model's process is starting again", "Restarting");
        case reading.badOutput:
            return KI18n.i18nc("@info a Model block whose model gave back something that is not sound", "Bad output");
        }
        return KI18n.i18nc("@info a Model block in a session that is not running", "Not running");
    }

    /// The caption of the tile: the state, with the delay while it runs.
    readonly property string caption: reading.sounding
        ? KI18n.i18nc("@info a Model block running, and how late the sound is, 'Running, 63 ms'", "Running, %1 ms",
                      Math.round(reading.latencyMs))
        : reading.stateWord

    /// **Why nothing is coming out**, for every state that is silent, or
    /// nothing while sound leaves the block.
    readonly property string silence: {
        if (reading.sounding) {
            return "";
        }
        if (!reading.blockEnabled) {
            return KI18n.i18nc("@info why a Model block is silent",
                               "Silent: this block is switched off. No sound passes through it.");
        }
        if (reading.folder === "" || reading.state === reading.noModel) {
            return KI18n.i18nc("@info why a Model block is silent",
                               "Silent: no model is chosen. Sound only leaves this block after a model has processed it.");
        }
        switch (reading.state) {
        case reading.loading:
            return KI18n.i18nc("@info why a Model block is silent", "Silent: the model is still loading.");
        case reading.cannotKeepUp:
            return reading.detail === reading.cycleTooLong
                ? KI18n.i18nc("@info why a Model block is silent",
                              "Silent: the session's cycle is longer than Spare time allows. Choose a longer Spare time.")
                : KI18n.i18nc("@info why a Model block is silent",
                              "Silent for now: the model is not finishing in time, and nothing else may leave this block.");
        case reading.failed:
            return KI18n.i18nc("@info why a Model block is silent, then the reason, 'Silent: the folder is not there.'",
                               "Silent: %1", reading.refusalWords(reading.detail));
        case reading.restarting:
            return KI18n.i18nc("@info why a Model block is silent",
                               "Silent: the model's process stopped, and it is being started again.");
        case reading.badOutput:
            return KI18n.i18nc("@info why a Model block is silent",
                               "Silent: the model gave back something that is not sound.");
        }
        return KI18n.i18nc("@info why a Model block is silent", "Silent: the session is not running.");
    }

    /// Why a folder or a model will not run.
    function refusalWords(refusal: int): string {
        switch (refusal) {
        case 1:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "The folder is not there, or cannot be read.");
        case 2:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "The folder holds no model this version can read.");
        case 3:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "reverblabr-model.json cannot be read, or a value in it is out of range.");
        case 4:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "The model file is larger than 2 GiB.");
        case 5:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "The file is damaged, or is not a model.");
        case 6:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "The folder only holds files that can run code when opened (.pt, .pth, .ckpt, .pkl). They are never opened. Use an ONNX version of the model.");
        case 7:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "The model asks for a file outside its folder, and that is refused.");
        case 8:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "This kind of model is recognised, but this version cannot run it yet.");
        case 9:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "The model's inputs and outputs are not what its description says.");
        case 10:
            return KI18n.i18nc("@info why a Model block's model cannot be run", "The model could not be loaded.");
        case 11:
            return KI18n.i18nc("@info why a Model block's model cannot be run", "The model's process crashed three times in a minute.");
        case 12:
            return KI18n.i18nc("@info why a Model block's model cannot be run", "The program that runs models could not be started.");
        case 13:
            return KI18n.i18nc("@info why a Model block's model cannot be run", "The model took more than two minutes to load.");
        case 14:
            return KI18n.i18nc("@info why a Model block's folder cannot be used", "This model is of an unknown kind. Add a reverblabr-model.json to its folder to describe it.");
        case 15:
            return KI18n.i18nc("@info why a Model block's model cannot be run", "The model's sample rate cannot be converted from the session's.");
        }
        return KI18n.i18nc("@info why a Model block's model cannot be run", "The model will not run.");
    }

    /// What kind of model a folder holds.
    function familyWords(family: int): string {
        switch (family) {
        case 1:
            return KI18n.i18nc("@info a kind of model", "Model described by its configuration");
        case 2:
            return KI18n.i18nc("@info a kind of model", "DeepFilterNet noise cleanup");
        case 3:
            return KI18n.i18nc("@info a kind of model", "Noise cleanup");
        case 4:
            return KI18n.i18nc("@info a kind of model", "Voice conversion");
        case 5:
            return KI18n.i18nc("@info a kind of model", "Speech through a language model");
        }
        return "";
    }

    /// \a url, a folder the picker chose, as the setting holds it.
    function choose(url: url) {
        SessionModel.setParam(reading.blockId, "folder", RecordingStatus.folderFromUrl(url));
    }

    function remove() {
        SessionModel.setParam(reading.blockId, "folder", "");
    }
}
