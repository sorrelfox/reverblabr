// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The About dialog, opened as a person opens it, from the window's own action:
// what it says, where each link goes, and that a keyboard reaches them all.
// Opening it over the real window matters: that is where a dialog's binding
// loop shows, and the suite fails on one.
//
// **No link is pressed.** A press goes to the web browser, so what is tried is
// where each one would go. A plain record stands in where the words matter;
// `ApplicationRecord` is the application's own, as main() makes it.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "About"
    when: windowShown

    readonly property var record: ({
            displayName: "Reverblabr",
            version: "1.2.3+gabc1234",
            shortDescription: "Capture, process and route audio on your desktop",
            copyrightStatement: "(c) 2026 Reverblabr contributors",
            components: [{
                    name: "Qt",
                    description: "Application framework",
                    version: "6.11.2",
                    webAddress: "https://www.qt.io"
                }, {
                    name: "PipeWire",
                    description: "Audio and video server",
                    version: "1.4.9",
                    webAddress: "https://pipewire.org"
                }]
        })

    Component {
        id: mainWindow

        Main {}
    }

    /// The dialog, opened from the window's action, showing the record.
    function openDialog(): var {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window, "the window is created");
        window.aboutAction.trigger();
        tryVerify(() => window.aboutDialog !== null && window.aboutDialog.opened, 2000, "the dialog never opened");
        window.aboutDialog.about = testCase.record;
        window.aboutDialog.bugReportUrl = "https://example.org/bugs";
        window.aboutDialog.licenceName = "Mozilla Public License 2.0";
        return window.aboutDialog;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    function test_itOpensFromTheMenuAndGoesWhenClosed() {
        const window = createTemporaryObject(mainWindow, testCase);
        compare(window.aboutDialog, null, "nothing is built until it is asked for");
        window.aboutAction.trigger();
        tryVerify(() => window.aboutDialog !== null && window.aboutDialog.opened, 2000, "the dialog never opened");
        window.aboutDialog.close();
        tryCompare(window, "aboutDialog", null, 2000, "the dialog stayed behind");
    }

    function test_itSaysWhatTheRecordSays() {
        const dialog = testCase.openDialog();
        compare(dialog.title, "About Reverblabr");
        compare(dialog.nameLabel.text, "Reverblabr");
        compare(dialog.versionLabel.text, "Version 1.2.3+gabc1234");
        compare(dialog.descriptionLabel.text, "Capture, process and route audio on your desktop");
        compare(dialog.licenceButton.text, "Mozilla Public License 2.0");
        compare(dialog.copyrightLabel.text, "(c) 2026 Reverblabr contributors");
    }

    function test_everyLinkGoesToItsPage() {
        const dialog = testCase.openDialog();
        compare(dialog.websiteButton.target, AppInfo.homepageUrl);
        compare(dialog.bugButton.target, dialog.bugReportUrl);
        compare(dialog.contributeButton.target, AppInfo.contributeUrl);
        compare(dialog.licenceButton.target, AppInfo.licenceUrl);
    }

    function test_everyLibraryIsListedWithItsLink() {
        const dialog = testCase.openDialog();
        compare(dialog.libraries.count, 2);
        const pipeWire = dialog.libraries.itemAt(1);
        compare(pipeWire.button.text, "PipeWire");
        compare(pipeWire.button.target, "https://pipewire.org");
    }

    function test_withNowhereToSendABugThereIsNoButton() {
        const dialog = testCase.openDialog();
        verify(dialog.bugButton.visible);
        dialog.bugReportUrl = "";
        verify(!dialog.bugButton.visible);
    }

    function test_aRecordWithNoLicenceLeavesTheLinkOut() {
        const dialog = testCase.openDialog();
        verify(dialog.licenceButton.visible);
        dialog.licenceName = "";
        verify(!dialog.licenceButton.visible);
    }

    function test_aKeyboardReachesEveryLink_data() {
        return [{ tag: "website", button: "websiteButton" },
                { tag: "bug", button: "bugButton" },
                { tag: "contribute", button: "contributeButton" },
                { tag: "licence", button: "licenceButton" }];
    }

    function test_aKeyboardReachesEveryLink(data) {
        const dialog = testCase.openDialog();
        const link = dialog[data.button];
        compare(link.focusPolicy & Qt.TabFocus, Qt.TabFocus, "Tab passes it by");
        verify(link.text !== "", "a screen reader would have no name for it");
    }

    function test_aTextLinkSaysWhatItIs() {
        const link = testCase.openDialog().licenceButton;
        compare(link.Accessible.role, Accessible.Link);
        compare(link.Accessible.name, "Mozilla Public License 2.0");
    }

    function test_theApplicationsOwnRecordReads() {
        const dialog = testCase.openDialog();
        dialog.about = ApplicationRecord;
        compare(dialog.nameLabel.text, "Reverblabr");
        compare(dialog.versionLabel.text, "Version 1.2.3");
        compare(dialog.descriptionLabel.text, "What it does");
        compare(dialog.copyrightLabel.text, "The makers");
        const names = [];
        for (let index = 0; index < dialog.libraries.count; ++index) {
            const row = dialog.libraries.itemAt(index);
            names.push(row.button.text);
            verify(row.button.target.toString().startsWith("https://"), `${row.button.text} goes nowhere`);
        }
        compare(names, ["Qt", "KDE Frameworks", "PipeWire"]);
    }

    function test_aLinkIsReadInBothAppearances_data() {
        return [{ tag: "dark", mode: Appearance.ReverblabrDark },
                { tag: "light", mode: Appearance.ReverblabrLight }];
    }

    function test_aLinkIsReadInBothAppearances(data) {
        Appearance.mode = data.mode;
        const dialog = testCase.openDialog();
        compare(dialog.licenceButton.contentItem.color, AppColors.accentText);
        compare(dialog.licenceButton.contentItem.font.family, AppFonts.body.family);
        compare(dialog.licenceButton.contentItem.font.pointSize, AppFonts.body.pointSize);
    }

    function test_noStockAboutItem() {
        // The dialog is the application's own: nothing of Kirigami's page.
        const dialog = testCase.openDialog();
        const seen = [dialog.contentItem];
        while (seen.length > 0) {
            const item = seen.pop();
            verify(!(item instanceof Kirigami.AboutItem), "Kirigami's About page is back");
            for (let index = 0; index < item.children.length; ++index) {
                seen.push(item.children[index]);
            }
        }
    }
}
