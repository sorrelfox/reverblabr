// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the Virtual Devices window lists and lets somebody do: the
// outputs and the inputs, each under what it is for; adding one, renaming,
// switching and deleting it; what a refusal, an unreadable file or an
// every-user device looks like; and all of it by keyboard alone.
//
// **Against the mock**, which answers at once. The moment a change
// is waiting for its answer is shown on a row built on its own, with the
// `pending` the model would give it.
//
// **Devices for all users** go to a stand-in for the helper,
// `TestHelper`, which answers as the test tells it — let through, or any of
// the ways a password prompt can end — and can hold a request, which is how
// the window is looked at while the password is being asked for.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "VirtualDevices"
    when: windowShown

    readonly property int noticeMs: 1000

    Component {
        id: devicesWindow

        VirtualDevicesWindow {
            visible: true
        }
    }

    function init() {
        TestEngine.clearFakeVirtualDevices();
        TestHelper.reset();
    }

    function cleanup() {
        TestHelper.release();
        TestHelper.reset();
        TestEngine.clearFakeVirtualDevices();
        Appearance.mode = SuiteAppearance;
    }

    /// The window, shown, in front, and with the engine's list in it.
    function openWindow(): var {
        const window = createTemporaryObject(devicesWindow, testCase);
        verify(window, "the window is created");
        tryVerify(() => VirtualDevices.ready, testCase.noticeMs, "the engine never listed its devices");
        window.requestActivate();
        tryVerify(() => window.active, testCase.noticeMs, "the window never came to the front");
        return window;
    }

    function fake(json: var): string {
        return TestEngine.addFakeVirtualDevice(JSON.stringify(json));
    }

    /// The row the device \a deviceId is shown by, in \a section.
    function rowOf(section: var, deviceId: string): var {
        const row = section.devices.rowOf(deviceId);
        verify(row >= 0, "the device is not in its list");
        return section.list.itemAtIndex(row);
    }

    function modelRow(deviceId: string): var {
        return VirtualDevices.get(VirtualDevices.indexOf(deviceId));
    }

    function type(text: string) {
        for (const character of text) {
            keyClick(character);
        }
    }

    // --- what it shows ---

    function test_anEmptyWindowSaysWhatTheTwoKindsAreFor() {
        const window = testCase.openWindow();
        verify(window.empty, "an engine with no devices is not an empty window");
        verify(window.placeholder.visible, "nothing says what the window is for");
        verify(window.placeholder.text.length > 0);
        verify(window.addButton.enabled, "nothing to add one with");
    }

    function test_eachKindIsListedUnderWhatItIsFor() {
        const first = testCase.fake({name: "Every app", direction: "output"});
        const microphone = testCase.fake({name: "Processed voice", direction: "input", channels: "mono"});
        const second = testCase.fake({name: "Music only", direction: "output"});
        const window = testCase.openWindow();

        verify(!window.placeholder.visible, "the empty-window message is over a list");
        compare(window.outputs.list.count, 2);
        compare(window.inputs.list.count, 1);
        verify(!window.outputs.emptyLabel.visible);
        verify(!window.inputs.emptyLabel.visible);

        const row = testCase.rowOf(window.inputs, microphone);
        compare(row.name, "Processed voice");
        compare(row.detailLabel.text, "Mono", "a present device says only its channels");
        compare(testCase.rowOf(window.outputs, second).detailLabel.text, "Stereo");
        verify(!testCase.rowOf(window.outputs, first).scopeBadge.visible, "the user's own device says who it is for");
    }

    function test_aKindWithNoneSaysSo() {
        testCase.fake({name: "Every app", direction: "output"});
        const window = testCase.openWindow();
        verify(window.inputs.emptyLabel.visible, "an empty list of inputs says nothing");
        verify(!window.inputs.list.visible);
        verify(window.inputs.visible, "the inputs' heading went with its devices");
    }

    function test_anEveryUserDeviceIsChangedThroughThePassword() {
        const shared = testCase.fake({name: "Studio feed", scope: "system"});
        const window = testCase.openWindow();
        const row = testCase.rowOf(window.outputs, shared);

        verify(row.scopeBadge.visible, "nothing says it is every user's");
        verify(row.forAllUsers);
        compare(VirtualDevices.promptWindow, window, "the password prompt would not belong to this window");
        // **Offered like any other**: the model sends it through the
        // helper and the password, which the row need not know.
        verify(row.enabledSwitch.enabled, "every user's device cannot be switched");
        verify(row.renameButton.enabled, "every user's device cannot be renamed");
        verify(row.deleteButton.enabled, "every user's device cannot be deleted");

        // There is no helper in the tests: the change comes back refused,
        // and says so in the window like any other.
        mouseClick(row.enabledSwitch);
        tryVerify(() => window.refusedMessage.visible, testCase.noticeMs, "the refused change was not said");
        compare(testCase.modelRow(shared).enabled, true);
        verify(testCase.rowOf(window.outputs, shared).enabledSwitch.checked,
               "the switch shows what was asked for, not what happened");
    }

    function test_theAddDialogCanAskForADeviceForEveryUser() {
        const window = testCase.openWindow();
        const dialog = window.addDialog;
        dialog.open();
        tryVerify(() => dialog.opened, testCase.noticeMs);
        verify(dialog.onlyMeChoice.checked, "the user's own device is not the first choice");
        verify(!dialog.forAllUsers, "a device for every user is the first choice");
        verify(dialog.allUsersChoice.enabled, "every user cannot be chosen, with the helper built");

        mouseClick(dialog.allUsersChoice);
        verify(dialog.forAllUsers, "choosing every user did not");
        verify(!dialog.onlyMeChoice.checked);
        TestHelper.answer = "";
        dialog.nameField.text = "Studio feed";
        mouseClick(dialog.inputChoice);
        dialog.addAction.trigger();

        tryCompare(VirtualDevices, "count", 1);
        compare(TestHelper.requests, 1, "the helper was not asked");
        const made = VirtualDevices.get(0);
        compare(made.name, "Studio feed");
        compare(made.scope, "system", "the device was made for this user only");
        compare(made.direction, "input");
        verify(!window.refusedMessage.visible);
        tryVerify(() => window.inputs.list.activeFocus, testCase.noticeMs, "the new device was not selected");

        // Opened again, it starts over at the user's own.
        dialog.open();
        tryVerify(() => dialog.opened, testCase.noticeMs);
        verify(dialog.onlyMeChoice.checked, "the last choice of every user stuck");
        verify(!dialog.forAllUsers);
        dialog.close();
    }

    function test_theUsersOwnDeviceNeverAsksForThePassword() {
        const window = testCase.openWindow();
        window.addDialog.open();
        tryVerify(() => window.addDialog.opened, testCase.noticeMs);
        window.addDialog.nameField.text = "Mine";
        window.addDialog.addAction.trigger();
        tryCompare(VirtualDevices, "count", 1);
        compare(VirtualDevices.get(0).scope, "user");
        compare(TestHelper.requests, 0, "the helper was asked for the user's own device");
    }

    function test_everyUserCannotBeChosenWithoutTheHelper() {
        const window = testCase.openWindow();
        window.allUsersAvailable = false;
        window.addDialog.open();
        tryVerify(() => window.addDialog.opened, testCase.noticeMs);
        verify(!window.addDialog.allUsersChoice.enabled, "every user can be chosen with no helper to ask");
        verify(window.addDialog.allUsersChoice.whyNot.length > 0, "nothing says why it can't be chosen");
        compare(window.addDialog.allUsersChoice.Accessible.description, window.addDialog.allUsersChoice.whyNot);
        verify(window.addDialog.onlyMeChoice.checked);
        window.addDialog.close();
    }

    function test_onlyAnEveryUserRowCarriesThePadlock() {
        const mine = testCase.fake({name: "Mine"});
        const shared = testCase.fake({name: "Studio feed", scope: "system"});
        const window = testCase.openWindow();
        const own = testCase.rowOf(window.outputs, mine);
        const theirs = testCase.rowOf(window.outputs, shared);

        verify(!own.lockMarker.visible, "the user's own device says it needs a password");
        verify(theirs.lockMarker.visible, "an every-user device does not say it needs a password");
        verify(theirs.lockMarker.Accessible.name.length > 0, "the padlock is silent to a screen reader");
        for (const control of [theirs.enabledSwitch, theirs.renameButton, theirs.deleteButton]) {
            compare(control.Accessible.description, theirs.passwordNote, `${control} does not say it asks for one`);
            verify(control.QQC2.ToolTip.text.indexOf("password") >= 0, `${control}'s tooltip does not say so`);
        }
        for (const control of [own.enabledSwitch, own.renameButton, own.deleteButton]) {
            compare(control.Accessible.description, "", `${control} of the user's own says it needs a password`);
        }
    }

    function test_waitingForThePasswordIsSaidAndOtherChangesAreNotOffered() {
        const first = testCase.fake({name: "Studio feed", scope: "system"});
        const second = testCase.fake({name: "Lobby", scope: "system"});
        const mine = testCase.fake({name: "Mine"});
        const window = testCase.openWindow();
        verify(!window.authorizingMessage.visible);

        TestHelper.hold = true;
        TestHelper.answer = "";
        mouseClick(testCase.rowOf(window.outputs, first).enabledSwitch);
        tryVerify(() => VirtualDevices.authorizing, testCase.noticeMs, "the helper was not asked");

        verify(window.authorizingMessage.visible, "nothing says the password is being asked for");
        const asked = testCase.rowOf(window.outputs, first);
        verify(asked.detailLabel.text.indexOf("password") >= 0, "the row does not say what it waits for");
        verify(!asked.enabledSwitch.enabled, "the row waiting for the password can be switched again");
        const other = testCase.rowOf(window.outputs, second);
        verify(!other.enabledSwitch.enabled, "another every-user device can be switched meanwhile");
        verify(!other.renameButton.enabled, "another every-user device can be renamed meanwhile");
        verify(!other.deleteButton.enabled, "another every-user device can be deleted meanwhile");
        other.startRenaming();
        verify(!other.renaming, "another every-user device starts renaming meanwhile");
        verify(testCase.rowOf(window.outputs, mine).enabledSwitch.enabled, "the user's own device is held up too");

        window.addDialog.open();
        tryVerify(() => window.addDialog.opened, testCase.noticeMs);
        verify(!window.addDialog.allUsersChoice.enabled, "a second every-user change is offered");
        verify(window.addDialog.allUsersChoice.whyNot.length > 0);
        window.addDialog.close();

        TestHelper.release();
        tryVerify(() => !VirtualDevices.authorizing, testCase.noticeMs);
        verify(!window.authorizingMessage.visible, "the waiting notice outlived the wait");
        tryVerify(() => testCase.modelRow(first).enabled === false, testCase.noticeMs,
                  "the helper's list never reached the window");
        verify(testCase.rowOf(window.outputs, second).enabledSwitch.enabled, "the other device stayed locked");
    }

    function test_eachWayThePasswordCanFailIsSaid() {
        const shared = testCase.fake({name: "Studio feed", scope: "system"});
        const window = testCase.openWindow();
        for (const reason of ["AuthorizationCancelled", "AuthorizationDenied", "HelperUnavailable", "HelperFailed",
                              "ChangedElsewhere"]) {
            window.refusedMessage.visible = false;
            TestHelper.answer = reason;
            window.rename(shared, "Renamed");
            tryVerify(() => window.refusedMessage.visible, testCase.noticeMs, `${reason} was not said`);
            const text = window.refusedMessage.text;
            verify(text.indexOf("Studio feed") >= 0, `${reason}: the message does not name the device: ${text}`);
            verify(text.indexOf(VirtualDevices.reasonText(reason)) >= 0, `${reason}: the message does not say why`);
            verify(text.indexOf(reason) < 0, `${reason}: the helper's own word reached a reader`);
            compare(window.refusedMessage.type,
                    reason === "AuthorizationCancelled" ? Kirigami.MessageType.Information : Kirigami.MessageType.Error,
                    `${reason}: the wrong kind of message`);
            compare(testCase.modelRow(shared).name, "Studio feed", `${reason} changed the name`);
            verify(!VirtualDevices.authorizing);
        }
        compare(TestHelper.requests, 5);
    }

    function test_theScopeIsChosenByKeyboard() {
        const window = testCase.openWindow();
        window.addDialog.open();
        tryVerify(() => window.addDialog.opened, testCase.noticeMs);
        const choice = window.addDialog.allUsersChoice;
        choice.forceActiveFocus();
        keyClick(Qt.Key_Space);
        verify(window.addDialog.forAllUsers, "Space did not choose every user");
        window.addDialog.onlyMeChoice.forceActiveFocus();
        keyClick(Qt.Key_Space);
        verify(!window.addDialog.forAllUsers, "Space did not choose only me");
        for (const control of [window.addDialog.onlyMeChoice, choice]) {
            verify(control.Accessible.name.length > 0, `${control} has no accessible name`);
            compare(control.focusPolicy & Qt.TabFocus, Qt.TabFocus, `${control} can't be reached with Tab`);
        }
        window.addDialog.close();
    }

    /// The colours the desktop fills a chosen thing with.
    Item {
        id: selectionColours

        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.Selection
    }

    // **Which option is chosen can be seen**. Under the desktop's
    // look the style's own checked button was a shade of grey next to its
    // neighbour's; the chosen one now takes the selection's colours, as the
    // Volume block's overdrive does. The Reverblabr style fills it with the
    // accent, as it always has.
    function test_theChosenOptionIsFilled() {
        const window = testCase.openWindow();
        const dialog = window.addDialog;
        dialog.open();
        tryVerify(() => dialog.opened, testCase.noticeMs);
        const pairs = [[dialog.outputChoice, dialog.inputChoice],
                       [dialog.stereoChoice, dialog.monoChoice],
                       [dialog.onlyMeChoice, dialog.allUsersChoice]];
        for (const [chosen, other] of pairs) {
            verify(chosen.checked && !other.checked, `${chosen.text} is not the first choice`);
            verify(chosen.highlighted, `the chosen ${chosen.text} is not filled`);
            verify(!other.highlighted, `${other.text} is filled but not chosen`);
            if (Appearance.followsSystem) {
                const fill = chosen.Kirigami.Theme.backgroundColor;
                verify(Qt.colorEqual(fill, selectionColours.Kirigami.Theme.backgroundColor),
                       `the chosen ${chosen.text} is ${fill}, not the selection's colour`);
                verify(Qt.colorEqual(chosen.Kirigami.Theme.textColor, selectionColours.Kirigami.Theme.textColor),
                       `the chosen ${chosen.text} is not written in the selection's text colour`);
                verify(!Qt.colorEqual(fill, other.Kirigami.Theme.backgroundColor),
                       `${chosen.text} and ${other.text} look the same`);
            } else {
                verify(Qt.colorEqual(chosen.background.color, AppColors.accent),
                       `the chosen ${chosen.text} is not in the accent`);
                verify(Qt.colorEqual(other.background.color, "transparent"),
                       `${other.text} is not left unfilled`);
            }
        }

        // The fill moves with the choice.
        mouseClick(dialog.monoChoice);
        verify(dialog.monoChoice.highlighted && !dialog.stereoChoice.highlighted, "the fill did not follow the choice");
        dialog.close();
    }

    function test_whereADeviceStandsIsSaid() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();

        TestEngine.setFakeVirtualDeviceStatus(id, "creating");
        tryCompare(testCase.rowOf(window.outputs, id).detailLabel, "text", "Stereo · Starting…");
        TestEngine.setFakeVirtualDeviceStatus(id, "waitingForPipeWire");
        tryCompare(testCase.rowOf(window.outputs, id).detailLabel, "text", "Stereo · Waiting for the audio system");
        TestEngine.setFakeVirtualDeviceStatus(id, "missing");
        const row = testCase.rowOf(window.outputs, id);
        tryVerify(() => row.detailLabel.text.indexOf("Missing") >= 0, testCase.noticeMs);
        compare(row.detailLabel.color, AppColors.warning, "a missing device does not stand out");
    }

    // --- adding ---

    function test_aDeviceIsAddedAndSelected() {
        const window = testCase.openWindow();
        mouseClick(window.addButton);
        const dialog = window.addDialog;
        tryVerify(() => dialog.opened, testCase.noticeMs, "the dialog did not open");
        verify(dialog.outputChoice.checked, "an output is not the first choice");
        verify(dialog.stereoChoice.checked, "stereo is not the first choice");
        verify(!dialog.addAction.enabled, "a device can be added with no name");

        dialog.nameField.text = "Processed voice";
        mouseClick(dialog.inputChoice);
        mouseClick(dialog.monoChoice);
        verify(dialog.addAction.enabled);
        dialog.addAction.trigger();
        tryVerify(() => !dialog.opened, testCase.noticeMs, "the dialog stayed open");

        tryCompare(VirtualDevices, "count", 1);
        const made = VirtualDevices.get(0);
        compare(made.name, "Processed voice");
        compare(made.direction, "input");
        compare(made.channels, "mono");
        compare(window.inputs.list.count, 1);
        tryVerify(() => window.inputs.list.activeFocus, testCase.noticeMs, "the new device was not selected");
        compare(window.inputs.list.currentIndex, window.inputs.devices.rowOf(made.deviceId));
    }

    function test_aNameOfSpacesIsNotAName() {
        const window = testCase.openWindow();
        window.addDialog.open();
        tryVerify(() => window.addDialog.opened, testCase.noticeMs);
        window.addDialog.nameField.text = "   ";
        verify(!window.addDialog.addAction.enabled, "a name of spaces can be added");
        window.addDialog.accept();
        compare(VirtualDevices.count, 0, "a device with no name was asked for");
    }

    function test_aNameIsNoLongerThanTheEngineTakes() {
        const window = testCase.openWindow();
        window.addDialog.open();
        tryVerify(() => window.addDialog.opened, testCase.noticeMs);
        window.addDialog.nameField.text = "x".repeat(VirtualDevices.maximumNameLength + 10);
        compare(window.addDialog.nameField.text.length, VirtualDevices.maximumNameLength,
                "the box takes a name the engine refuses");
        window.addDialog.addAction.trigger();
        tryCompare(VirtualDevices, "count", 1);
        compare(VirtualDevices.get(0).name.length, VirtualDevices.maximumNameLength);
    }

    function test_aRefusedDeviceIsSaidInTheWindowNotTheBanner() {
        const window = testCase.openWindow();
        TestEngine.refuseNextVirtualDeviceChange("FileNotWritable");
        window.addDialog.open();
        tryVerify(() => window.addDialog.opened, testCase.noticeMs);
        window.addDialog.nameField.text = "Every app";
        window.addDialog.addAction.trigger();

        tryVerify(() => window.refusedMessage.visible, testCase.noticeMs, "the refusal was not said");
        verify(window.refusedMessage.text.length > 0);
        verify(window.refusedMessage.text.indexOf("FileNotWritable") < 0, "the engine's own word reached a reader");
        compare(VirtualDevices.count, 0);
        verify(ErrorSurface.code !== "device.virtualNotCreated", "the main window's banner says it as well");
    }

    // --- renaming ---

    function test_renamingIsSentOnEnterAndDroppedOnEscape() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();
        const list = window.outputs.list;
        list.forceActiveFocus();
        let row = testCase.rowOf(window.outputs, id);

        keyClick(Qt.Key_F2);
        verify(row.renaming, "F2 does not rename the selected device");
        verify(row.nameField.activeFocus);
        compare(row.nameField.text, "Every app", "the box does not start at the name it has");
        row.nameField.selectAll();
        testCase.type("Games");
        keyClick(Qt.Key_Escape);
        verify(!row.renaming, "Escape left the box open");
        compare(testCase.modelRow(id).name, "Every app", "Escape sent the name");
        verify(list.activeFocus, "the focus did not go back to the list");

        // By a double-click on it, this time.
        mouseDoubleClickSequence(row);
        verify(row.renaming, "a double-click does not rename");
        row.nameField.selectAll();
        testCase.type("Games");
        keyClick(Qt.Key_Return);
        tryVerify(() => testCase.modelRow(id).name === "Games", testCase.noticeMs,
                  "the engine never had it: name Games");
        row = testCase.rowOf(window.outputs, id);
        compare(row.name, "Games");
        verify(!row.renaming);
    }

    function test_theRenameButtonRenamesToo() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();
        const row = testCase.rowOf(window.outputs, id);
        mouseClick(row.renameButton);
        verify(row.renaming, "the rename button does not rename");
        row.nameField.text = "All the apps";
        row.nameField.editingFinished();
        tryVerify(() => testCase.modelRow(id).name === "All the apps", testCase.noticeMs,
                  "the engine never had it: name All the apps");
    }

    // --- switching ---

    function test_theSwitchTurnsADeviceOffAndOn() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();
        let row = testCase.rowOf(window.outputs, id);
        verify(row.enabledSwitch.checked);

        mouseClick(row.enabledSwitch);
        tryVerify(() => testCase.modelRow(id).enabled === false, testCase.noticeMs,
                  "the engine never had it: enabled false");
        row = testCase.rowOf(window.outputs, id);
        verify(!row.enabledSwitch.checked, "the switch is not where the engine says");
        verify(row.detailLabel.text.indexOf("Off") >= 0, "an off device does not say so");

        // Space, on the selected row.
        window.outputs.list.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryVerify(() => testCase.modelRow(id).enabled === true, testCase.noticeMs,
                  "the engine never had it: enabled true");
        verify(testCase.rowOf(window.outputs, id).enabledSwitch.checked);
    }

    function test_aRefusedSwitchGoesBack() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();
        const row = testCase.rowOf(window.outputs, id);

        TestEngine.refuseNextVirtualDeviceChange("FileNotWritable");
        mouseClick(row.enabledSwitch);
        tryVerify(() => window.refusedMessage.visible, testCase.noticeMs, "the refusal was not said");
        compare(testCase.modelRow(id).enabled, true);
        verify(testCase.rowOf(window.outputs, id).enabledSwitch.checked,
               "the switch shows what was asked for, not what the engine did");
    }

    // --- deleting ---

    function test_deletingAsksFirst() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();
        const row = testCase.rowOf(window.outputs, id);

        mouseClick(row.deleteButton);
        const dialog = window.deleteDialog;
        tryVerify(() => dialog.opened, testCase.noticeMs, "the delete button did not ask");
        verify(dialog.subtitle.indexOf("Every app") >= 0, "the question does not name the device");
        dialog.close();
        tryVerify(() => !dialog.opened, testCase.noticeMs);
        compare(VirtualDevices.count, 1, "cancelling deleted it");

        // Delete, on the selected row, asks the same.
        window.outputs.list.forceActiveFocus();
        keyClick(Qt.Key_Delete);
        tryVerify(() => dialog.opened, testCase.noticeMs, "the Delete key did not ask");
        dialog.deleteAction.trigger();
        tryCompare(VirtualDevices, "count", 0);
        verify(window.placeholder.visible, "the last device gone, the window does not say so");
    }

    // --- what goes wrong ---

    function test_anUnreadableFileIsSaidInTheWindow() {
        testCase.fake({name: "Every app"});
        testCase.fake({name: "Studio feed", scope: "system"});
        const window = testCase.openWindow();

        TestEngine.setFakeVirtualDeviceFileUnreadable("user", "Malformed");
        tryVerify(() => window.userUnreadableMessage.visible, testCase.noticeMs, "an unreadable file is not said");
        verify(window.userUnreadableMessage.text.indexOf(VirtualDevices.reasonText("Malformed")) >= 0,
               "the notice does not say why");
        verify(!window.systemUnreadableMessage.visible);
        verify(!window.placeholder.visible, "devices hidden by an unreadable file read as none at all");

        TestEngine.setFakeVirtualDeviceFileUnreadable("system", "NotOurs");
        tryVerify(() => window.systemUnreadableMessage.visible, testCase.noticeMs);
        TestEngine.setFakeVirtualDeviceFileUnreadable("user", "");
        tryVerify(() => !window.userUnreadableMessage.visible, testCase.noticeMs, "the notice outlived the problem");
    }

    function test_aDeviceTheAudioSystemWouldNotMakeIsSaid() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();
        TestEngine.refuseFakeVirtualDeviceNode(id);
        tryVerify(() => window.refusedMessage.visible, testCase.noticeMs, "the refused node was not said");
        verify(window.refusedMessage.text.indexOf("Every app") >= 0, "the message does not name the device");
        verify(testCase.rowOf(window.outputs, id).detailLabel.text.indexOf("Missing") >= 0);
    }

    // --- a change waiting for its answer ---

    Component {
        id: pendingList

        ListView {
            width: 480
            height: 200
            model: ListModel {
                ListElement {
                    deviceId: "0a1b2c3d"
                    name: "Every app"
                    direction: "output"
                    channels: "stereo"
                    enabled: true
                    scope: "user"
                    status: "present"
                    pending: true
                }
            }
            delegate: VirtualDeviceRow {}
        }
    }

    function test_aRowWaitingForTheEngineTakesNoSecondChange() {
        const list = createTemporaryObject(pendingList, testCase);
        verify(list);
        list.forceLayout();
        const row = list.itemAtIndex(0);
        verify(row, "no row");
        verify(!row.actionable);
        verify(!row.enabledSwitch.enabled, "a waiting row can be switched again");
        verify(!row.renameButton.enabled, "a waiting row can be renamed again");
        verify(!row.deleteButton.enabled, "a waiting row can be deleted");
        verify(row.detailLabel.text.indexOf("Saving") >= 0, "nothing says a change is under way");
        verify(row.opacity < 1, "a waiting row looks like any other");
        row.startRenaming();
        verify(!row.renaming, "a waiting row starts renaming");
    }

    // --- the keyboard, and what a screen reader hears ---

    function test_everythingCanBeDoneByKeyboard() {
        const window = testCase.openWindow();
        window.addButton.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryVerify(() => window.addDialog.opened, testCase.noticeMs, "Space on Add did not open the dialog");
        tryVerify(() => window.addDialog.nameField.activeFocus, testCase.noticeMs, "the name box has no focus");
        testCase.type("Keys");
        keyClick(Qt.Key_Return);
        tryCompare(VirtualDevices, "count", 1);
        const id = VirtualDevices.get(0).deviceId;
        tryVerify(() => window.outputs.list.activeFocus, testCase.noticeMs, "the new device is not selected");

        testCase.fake({name: "Second"});
        keyClick(Qt.Key_Down);
        compare(window.outputs.list.currentIndex, 1, "the arrow keys do not move through the list");
        keyClick(Qt.Key_Up);
        compare(window.outputs.list.currentIndex, 0);

        keyClick(Qt.Key_F2);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        testCase.type("Renamed");
        keyClick(Qt.Key_Return);
        tryVerify(() => testCase.modelRow(id).name === "Renamed", testCase.noticeMs,
                  "the engine never had it: name Renamed");

        verify(!testCase.rowOf(window.outputs, id).nameField.activeFocus,
               "the hidden name box kept the keys after the rename");
        keyClick(Qt.Key_Space);
        tryVerify(() => testCase.modelRow(id).enabled === false, testCase.noticeMs,
                  "the engine never had it: enabled false");

        keyClick(Qt.Key_Delete);
        tryVerify(() => window.deleteDialog.opened, testCase.noticeMs);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !window.deleteDialog.opened, testCase.noticeMs, "Escape did not cancel");
        compare(VirtualDevices.count, 2);
    }

    function test_everyControlHasAName() {
        const id = testCase.fake({name: "Every app"});
        const window = testCase.openWindow();
        const row = testCase.rowOf(window.outputs, id);
        compare(row.Accessible.name, "Every app");
        verify(row.Accessible.description.length > 0);
        for (const control of [row.enabledSwitch, row.renameButton, row.deleteButton, row.nameField,
                               window.addButton, window.addDialog.nameField]) {
            verify(control.Accessible.name.length > 0, `${control} has no accessible name`);
        }
        verify(row.renameButton.Accessible.name.indexOf("Every app") >= 0, "the rename button does not say what");
        compare(window.outputs.list.Accessible.role, Accessible.List);
    }
}
