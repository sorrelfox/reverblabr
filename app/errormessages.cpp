// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "errormessages.h"

#include "core/errorcodes.h"

#include <KLazyLocalizedString>
#include <KLocalizedString>

#include <algorithm>
#include <array>

namespace rb::app
{

namespace
{

struct Message {
    std::string_view code;
    KLazyLocalizedString text;
};

// One translated message per engine error code (engine/core/errorcodes.h).
// %1, %2, ... are the code's arguments in the order the engine declares them.
constexpr std::array kMessages{
    Message{"block.unknownType",
            kli18nc("@info",
                    "This session contains a block of type “%1”, which this version of Reverblabr doesn't know. "
                    "The block is kept, but it can't run.")},
    Message{"graph.cycle",
            kli18nc("@info",
                    "The connections in this session form a loop. Remove one of the connections in the loop to run "
                    "the session.")},
    Message{"routing.feedbackLoop",
            kli18nc("@info",
                    "This connection would feed the session's output back into its own input, so it wasn't made. "
                    "The sound would have gone round: %1.")},
    Message{"session.corruptFile",
            kli18nc("@info", "The session file %1 is damaged and couldn't be opened. It was moved aside as %2.")},
    // **A session is named, not numbered**. %1 is the session's name
    // where the window remembers one, and otherwise the id or the path the
    // engine gave — which is not a name, so the sentence does not call it one.
    Message{"session.notFound",
            kli18nc("@info %1 is a session's name, or its id or file when there is no name to give",
                    "Reverblabr couldn't find the session “%1”. Its file may have been moved or deleted.")},
    Message{"session.notSaved",
            kli18nc("@info", "The session %1 couldn't be saved: %2. It's still open, so nothing is lost.")},
    Message{"session.cannotRun", kli18nc("@info", "The session %1 couldn't be started: %2.")},
    Message{"edit.refused", kli18nc("@info", "That change wasn't made: %1. The session is as it was.")},
    Message{"preset.refused", kli18nc("@info", "The preset “%1” wasn't changed: %2.")},
    Message{
        "device.virtualNotCreated",
        kli18nc("@info %1 is the name a virtual audio device was to have", "The virtual device “%1” wasn't made: %2.")},
    Message{"device.virtualRefused",
            kli18nc("@info %1 is a virtual audio device's name",
                    "The virtual device “%1” wasn't changed: %2. It's as it was.")},
    Message{"device.virtualNodeRefused",
            kli18nc("@info %1 is a virtual audio device's name",
                    "The audio system didn't make the virtual device “%1”. It's kept, and is tried again the next "
                    "time the audio system starts or the virtual devices change.")},
    Message{"recording.failed",
            kli18nc("@info %1 is a recording's file, with its folder",
                    "Recording to %1 stopped: %2. Anything already written to it is kept. Start the session again "
                    "to record again.")},
    Message{"recording.overflow",
            kli18nc("@info %1 is a recording's file, with its folder; %2 is a number of seconds",
                    "The disk didn't keep up, so %2 seconds are missing from the recording %1. It's still "
                    "recording.")},
    Message{"recording.refused", kli18nc("@info", "The recording wasn't paused, resumed or split: %1.")},
    Message{"recording.tagsNotWritten",
            kli18nc("@info %1 is a recording's file, with its folder",
                    "The recording %1 is complete, but its tags couldn't be written: it has no title, artist or "
                    "the rest.")},
    Message{"plugin.crashed",
            kli18nc("@info %1 is the plugin's name",
                    "Plugin crashed: %1 stopped, and the sound now goes around it. Restart it from its panel.")},
    Message{"plugin.stoppedResponding",
            kli18nc("@info %1 is the plugin's name",
                    "Plugin stopped responding: %1 was closed, and the sound now goes around it. Restart it from its "
                    "panel.")},
    Message{"plugin.restartRefused", kli18nc("@info", "The plugin wasn't restarted: %1.")},
    Message{
        "recording.previewFailed",
        kli18nc("@info %1 is a recording's file, with its folder; %2 is why", "The recording %1 can't be played: %2.")},
    Message{"recording.notFound",
            kli18nc("@info %1 is a recording's file, with its folder",
                    "The recording %1 isn't in the list of recordings any more: it was deleted or moved.")},
    Message{"recording.renameRefused",
            kli18nc("@info %1 is a recording's file, with its folder; %2 is why",
                    "The recording %1 wasn't renamed: %2. It's as it was.")},
    Message{"session.overloaded",
            kli18nc("@info %1 is a session's name",
                    "The session %1 needed more time than each audio cycle allows, so it went silent to keep "
                    "Reverblabr running. Take out or switch off its heaviest blocks, or raise the quantum, and it "
                    "plays again.")},
    Message{"engine.daemonUnreachable",
            kli18nc("@info %1 is why",
                    "Reverblabr's background engine couldn't be started: %1. The engine runs in this window "
                    "instead, so its sessions stop when the window closes.")},
    Message{"recording.tagsNotSaved",
            kli18nc("@info %1 is a recording's file, with its folder",
                    "The tags of the recording %1 couldn't be saved. The file is as it was.")},
    Message{"recording.lowDiskSpace",
            kli18nc("@info %1 is a recording's file, with its folder; %2 is a number of megabytes",
                    "Only %2 MB are left on the disk the recording %1 is on. It's still recording, and it stops "
                    "cleanly before the disk is full.")},
};

// Why something was refused, as the engine names it in a "reason" argument.
//
// **These are arguments, not codes.** The engine says session.cannotRun and
// puts one word in the reason — the name of the enumerator it has, because
// the engine has no idea what language anybody speaks. Somewhere that word
// has to become a phrase a person can read, and the message table is where
// the engine's vocabulary is turned into ours.
//
// A reason with no entry is shown as it came, which is a poor sentence but a
// true one: an engine that has learned a new refusal is better half read
// than silently rendered as "something went wrong".
constexpr std::array kReasons{
    Message{"UnknownBlockType",
            kli18nc("@info a reason a session would not start", "it uses a block this version doesn't have")},
    Message{"BlockRefused",
            kli18nc("@info a reason a session would not start", "one of its blocks can't run at this sample rate")},
    Message{"NotCompilable",
            kli18nc("@info a reason a session would not start",
                    "its blocks and connections don't make a signal path that can run")},
    Message{"NodeRefused", kli18nc("@info a reason a session would not start", "the audio system wouldn't take it")},
    Message{"NotRunning", kli18nc("@info a reason something was refused", "the session isn't running")},
    Message{"invalidName", kli18nc("@info a reason a recording was not renamed", "that isn't a name a file can have")},
    Message{"exists", kli18nc("@info a reason a recording was not renamed", "another file already has that name")},
    Message{"failed", kli18nc("@info a reason a recording was not renamed", "the disk refused")},
    Message{"unreadable", kli18nc("@info a reason a recording can't be played", "the file can't be read as sound")},
    Message{"noAudioSystem", kli18nc("@info a reason a recording can't be played", "the audio system isn't running")},
    Message{"audioSystemUnavailable",
            kli18nc("@info a reason a session would not start", "the audio system isn't running")},
    Message{"noSessionBus",
            kli18nc("@info a reason the background engine couldn't be started", "there is no session bus")},
    Message{"daemonNotInstalled",
            kli18nc("@info a reason the background engine couldn't be started", "it isn't installed")},
    Message{"daemonRefused", kli18nc("@info a reason the background engine couldn't be started", "it didn't start")},
    Message{"blockNotFound", kli18nc("@info a reason an edit was refused", "the block it names isn't in the session")},
    Message{"notAHostedPlugin",
            kli18nc("@info a reason a plugin was not restarted", "it isn't a plugin that is switched on")},
    Message{"blockIdInUse", kli18nc("@info a reason an edit was refused", "another block already has that name")},
    Message{"wireToItself", kli18nc("@info a reason an edit was refused", "a block can't be connected to itself")},
    Message{"wireAlreadyExists", kli18nc("@info a reason an edit was refused", "that connection is already there")},
    Message{"wireNotFound", kli18nc("@info a reason an edit was refused", "there's no such connection")},
    Message{"settingTypeMismatch",
            kli18nc("@info a reason an edit was refused", "that setting doesn't take a value of that kind")},
    Message{"settingValueNotAllowed",
            kli18nc("@info a reason an edit was refused", "that setting doesn't take that value")},
    // Presets.
    Message{"notFound", kli18nc("@info a reason a preset was refused", "there's no preset of that name")},
    Message{"badName",
            kli18nc("@info a reason a preset was refused",
                    "a preset's name is its file's, so it can't be empty or contain / \\ : * ? \" < > |")},
    Message{"alreadyExists", kli18nc("@info a reason a preset was refused", "a preset of that name is already there")},
    Message{"readOnly",
            kli18nc("@info a reason a preset was refused", "it's one of the presets that come with the application")},
    Message{"fileNotReadable", kli18nc("@info a reason a preset was refused", "its file couldn't be read")},
    Message{"fileNotWritable", kli18nc("@info a reason a preset was refused", "its file couldn't be written")},
    Message{"notJson", kli18nc("@info a reason a preset was refused", "its file isn't valid JSON")},
    Message{"notAPreset", kli18nc("@info a reason a preset was refused", "its file isn't a preset")},
    Message{"wrongType", kli18nc("@info a reason a preset was refused", "it's for another kind of block")},
    // Virtual devices (ADR-0013): a pw::VirtualDeviceError's name, which is
    // why these are capitalised where the preset reasons above are not.
    Message{"EmptyName", kli18nc("@info a reason a virtual device was refused", "a device needs a name")},
    Message{"NameTooLong", kli18nc("@info a reason a virtual device was refused", "the name is too long")},
    Message{"NameHasControlCharacter",
            kli18nc("@info a reason a virtual device was refused", "a name can't contain line breaks or tabs")},
    Message{"NameNotTrimmed",
            kli18nc("@info a reason a virtual device was refused", "a name can't start or end with a space")},
    Message{"InvalidId", kli18nc("@info a reason a virtual device was refused", "there's no such device")},
    Message{"DuplicateId",
            kli18nc("@info a reason a virtual device was refused", "two devices would have the same identifier")},
    Message{"WrongScope",
            kli18nc("@info a reason a virtual device was refused",
                    "a device for all users can't be kept with your own, or yours with theirs")},
    Message{"TooManyDevices",
            kli18nc("@info a reason a virtual device was refused", "there are already as many devices as can be kept")},
    Message{"NotOurs",
            kli18nc("@info a reason a virtual device was refused",
                    "the file the devices are kept in wasn't written by Reverblabr, so it's left alone")},
    Message{"Malformed",
            kli18nc("@info a reason a virtual device was refused",
                    "the file the devices are kept in is damaged, so it's left alone")},
    Message{
        "FileNotReadable",
        kli18nc("@info a reason a virtual device was refused", "the file the devices are kept in couldn't be read")},
    Message{
        "FileNotWritable",
        kli18nc("@info a reason a virtual device was refused", "the file the devices are kept in couldn't be written")},
    Message{"NotFound", kli18nc("@info a reason a virtual device was refused", "there's no such device")},
    Message{"ReadOnlyScope",
            kli18nc("@info a reason a virtual device was refused",
                    "it's a device for all users, which needs an administrator to change")},
    Message{
        "BadRequest",
        kli18nc("@info a reason a virtual device was refused", "the request didn't say what kind of device to make")},
    // All-users devices: the helper's own reason, and the password
    // prompt's answers.
    Message{"ChangedElsewhere",
            kli18nc("@info a reason a virtual device for all users was refused",
                    "the devices for all users were changed elsewhere in the meantime; the list now shows them")},
    Message{"AuthorizationCancelled",
            kli18nc("@info a reason a virtual device for all users was refused",
                    "the administrator's password was cancelled")},
    Message{"AuthorizationDenied",
            kli18nc("@info a reason a virtual device for all users was refused",
                    "the administrator's password wasn't accepted")},
    Message{"HelperUnavailable",
            kli18nc("@info a reason a virtual device for all users was refused",
                    "the part of Reverblabr that changes devices for all users isn't installed")},
    Message{"HelperFailed",
            kli18nc("@info a reason a virtual device for all users was refused",
                    "the part of Reverblabr that changes devices for all users failed")},
    Message{"AuthorizationPending",
            kli18nc("@info a reason a virtual device for all users was refused",
                    "another change for all users is still waiting for the administrator's password")},
    // A recording's file: a record::FileError's name.
    Message{"FolderNotCreated", kli18nc("@info a reason a recording stopped", "its folder couldn't be made")},
    Message{"FileExists", kli18nc("@info a reason a recording stopped", "a file of that name is already there")},
    Message{"FileNotCreated",
            kli18nc("@info a reason a recording stopped", "the file couldn't be made there; check the folder")},
    Message{"UnsupportedFormat",
            kli18nc("@info a reason a recording stopped",
                    "that format can't be written at this sample rate or with these channels")},
    Message{"DiskFull",
            kli18nc("@info a reason a recording stopped",
                    "the disk is full, or so nearly full that the file was finished while it still could be")},
    Message{"WriteFailed", kli18nc("@info a reason a recording stopped", "writing to the disk failed")},
    // An M4A format whose encoder this system's FFmpeg does not have.
    // The same words stand in the catalogue's "encoderMissing", for a panel
    // to say why the format is not offered.
    Message{"EncoderMissing",
            kli18nc("@info a reason a recording stopped",
                    "this system's FFmpeg has no encoder for that format. Install the full FFmpeg (on Fedora, "
                    "from RPM Fusion), or record to Opus or FLAC instead")},
    Message{"encoderMissing",
            kli18nc("@info why a recording format is not offered",
                    "this system's FFmpeg has no encoder for it. Install the full FFmpeg (on Fedora, from RPM "
                    "Fusion), or record to Opus or FLAC instead")},
    // Pausing, resuming or splitting a recording.
    Message{"notARecorder", kli18nc("@info a reason a recording command was refused", "that block isn't a Recorder")},
    Message{"unknownAction",
            kli18nc("@info a reason a recording command was refused", "a recording can be paused, resumed or split")},
};

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// Applies the translation domain the GUI's catalogue lives in.
KLocalizedString inDomain(const KLazyLocalizedString &text)
{
    KLocalizedString localized = text;
#ifdef TRANSLATION_DOMAIN
    localized = localized.withDomain(TRANSLATION_DOMAIN);
#endif
    return localized;
}

const Message *findMessage(const QString &code)
{
    const auto it =
        std::ranges::find_if(kMessages, [&code](const Message &message) { return code == toQString(message.code); });
    return it == kMessages.end() ? nullptr : &*it;
}

const Message *findReason(const QString &reason)
{
    const auto it =
        std::ranges::find_if(kReasons, [&reason](const Message &message) { return reason == toQString(message.code); });
    return it == kReasons.end() ? nullptr : &*it;
}

const core::ErrorCodeInfo *findEngineCode(const QString &code)
{
    const auto it = std::ranges::find_if(
        core::kErrorCodes, [&code](const core::ErrorCodeInfo &info) { return code == toQString(info.name); });
    return it == core::kErrorCodes.end() ? nullptr : &*it;
}

} // namespace

QString errorMessage(const QString &code, const QVariantMap &args)
{
    const Message *message = findMessage(code);
    const core::ErrorCodeInfo *engineCode = findEngineCode(code);
    if (!message || !engineCode) {
        return i18nc("@info", "Something went wrong in the audio engine (error “%1”).", code);
    }

    KLocalizedString text = inDomain(message->text);
    for (const std::string_view name : engineCode->arguments) {
        if (!name.empty()) {
            const QString value = args.value(toQString(name)).toString();
            // A reason is the engine's own vocabulary and not a name, so it
            // is translated on the way into the sentence that carries it.
            text = text.subs(name == "reason" ? reasonMessage(value) : value);
        }
    }
    return text.toString();
}

QString reasonMessage(const QString &reason)
{
    const Message *message = findReason(reason);
    return message == nullptr ? reason : inDomain(message->text).toString();
}

bool hasErrorMessage(const QString &code)
{
    return findMessage(code) != nullptr;
}

QStringList errorMessageCodes()
{
    QStringList codes;
    codes.reserve(static_cast<qsizetype>(kMessages.size()));
    for (const Message &message : kMessages) {
        codes.append(toQString(message.code));
    }
    return codes;
}

} // namespace rb::app
