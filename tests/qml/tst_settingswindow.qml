// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Settings window: opened from the Settings menu of both menus and by
// its key, one of it however often it is asked for, framed as the main
// window is; a section each for the language, the appearance and Auto Run,
// each choice the same setting as the menus'; the restart row while a
// choice waits for one, and Restart Now. The suite's harness has three
// languages with a catalog, whatever the machine has installed, and a
// restart that starts nothing (tst_qml.cpp).
//
// **What only a compositor can show is not here**, as in tst_titlebar.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SettingsWindow"
    when: windowShown

    Component {
        id: mainWindow

        Main {}
    }

    function openWindow(): var {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window, "the main window is created");
        tryVerify(() => AppShell.hasSession, 1000, "the window came up with a session");
        return window;
    }

    /// Opens the main window and its Settings window, tall enough for the
    /// whole page: the size a test before left it at is kept.
    function openSettings(): var {
        const window = testCase.openWindow();
        window.settingsWindowAction.trigger();
        tryVerify(() => window.settingsWindow !== null && window.settingsWindow.visible, 1000,
                  "the Settings window did not open");
        window.settingsWindow.width = Kirigami.Units.gridUnit * 32;
        window.settingsWindow.height = Kirigami.Units.gridUnit * 48;
        return window;
    }

    /// Clicks \a item of \a settings once it is in view, as a person
    /// would scroll to it first.
    function click(settings: var, item: var) {
        settings.reveal(item);
        mouseClick(item);
    }

    /// The Settings menu of \a menus, a menu bar or the header's menu.
    function settingsMenu(menus: var): var {
        for (let i = 0; i < menus.count; ++i) {
            const menu = menus.menuAt(i);
            if (menu.title === "Se&ttings") {
                return menu;
            }
        }
        return null;
    }

    /// The row of the language combo box with \a code.
    function languageRow(settings: var, code: string): int {
        return settings.languageBox.indexOfValue(code);
    }

    /// A language with a catalog that is not the one in force, whatever the
    /// environment the suite runs in gives.
    function otherLanguage(): string {
        return LanguageSetting.inForce === "de" ? "pt_BR" : "de";
    }

    function cleanup() {
        LanguageSetting.chosen = "";
        Appearance.mode = SuiteAppearance;
        AutoRunSetting.everyEngineStart = false;
        AppShell.engineInBackground = true;
        AppShell.sandboxed = false;
        TestRestart.reset();
    }

    function test_theSettingsMenuEndsWithIt() {
        const window = testCase.openWindow();
        compare(window.settingsWindow, null, "the window was made before anybody asked for it");
        compare(window.settingsWindowAction.text, "&Configure Reverblabr…");
        compare(window.settingsWindowAction.icon.name, "configure");
        compare(window.settingsWindowAction.shortcut, StandardKey.Preferences);
        // **Last, after a rule, in the menu bar and in the header's menu
        // alike**: tst_titlebar holds the two together entry for entry.
        for (const menus of [window.menus, window.mainMenu]) {
            const settings = testCase.settingsMenu(menus);
            verify(settings !== null, "no Settings menu");
            const last = settings.count - 1;
            verify(settings.itemAt(last).action === window.settingsWindowAction, "Configure is not last");
            verify(settings.itemAt(last - 1) instanceof QQC2.MenuSeparator, "no rule before Configure");
        }
        // **Its accelerator is free in that menu**: no other entry is on C.
        const settings = testCase.settingsMenu(window.menus);
        for (let i = 0; i < settings.count - 1; ++i) {
            const item = settings.itemAt(i);
            const words = settings.menuAt(i) !== null ? settings.menuAt(i).title : (item.text ?? "");
            verify(!words.toLowerCase().includes("&c"), "two entries on C: " + words);
        }
    }

    function test_theActionOpensOneWindowRaisedIfAskedAgain() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        compare(settings.title, "Settings");
        verify(settings.transientParent === window, "the window does not belong to the main window");

        window.settingsWindowAction.trigger();
        verify(window.settingsWindow === settings, "a second Settings window");
        verify(settings.visible);

        settings.close();
        tryVerify(() => !settings.visible, 1000, "the window did not close");
        window.settingsWindowAction.trigger();
        verify(window.settingsWindow === settings, "closing it lost the window");
        tryVerify(() => settings.visible, 1000, "closed, the window did not come back");
        settings.close();
    }

    function test_itsKeyOpensIt() {
        const window = testCase.openWindow();
        if (Appearance.shortcutText(StandardKey.Preferences) === "") {
            skip("this platform has no key for Preferences");
        }
        window.requestActivate();
        tryVerify(() => window.active, 1000);
        keySequence(StandardKey.Preferences);
        tryVerify(() => window.settingsWindow !== null && window.settingsWindow.visible, 1000,
                  "Preferences' key opened no window");
        window.settingsWindow.close();
    }

    function test_itIsFramedAsTheMainWindowIs() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        const frameless = (settings.flags & Qt.FramelessWindowHint) !== 0;
        compare(frameless, window.ownTitleBar, "the window's frame is not the main window's kind");
        compare(settings.titleBar.visible, window.ownTitleBar);
        compare(settings.chrome.active, window.ownTitleBar);
        if (window.ownTitleBar) {
            compare(settings.menuBar, settings.titleBar);
            compare(settings.titleBar.height, AppMetrics.headerHeight);
            verify(settings.titleBar.closeButton.visible);
            mouseClick(settings.titleBar.closeButton);
            tryVerify(() => !settings.visible, 1000, "the title bar's close did not close the window");
        } else {
            settings.close();
        }
    }

    function test_itOpensAtTheSizeItWasLeftAt() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        settings.width = settings.minimumWidth + 41;
        settings.height = settings.minimumHeight + 29;
        tryCompare(settings, "width", settings.minimumWidth + 41);
        settings.close();
        const saved = WindowState.windowSize(settings.stateName);
        compare(saved.width, settings.minimumWidth + 41, "closing did not write the width down");
        compare(saved.height, settings.minimumHeight + 29, "closing did not write the height down");
    }

    function test_theThreeSectionsAreThere() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        compare(settings.languageSection.title, "Language");
        compare(settings.appearanceSection.title, "Appearance");
        compare(settings.autoRunSection.title, "Auto Run");
        for (const section of [settings.languageSection, settings.appearanceSection, settings.autoRunSection]) {
            verify(section.visible, section.title + " is not shown");
        }
        // In that order, down the page.
        verify(settings.appearanceSection.y > settings.languageSection.y);
        verify(settings.autoRunSection.y > settings.appearanceSection.y);
        // **What follows the desktop whatever is chosen** is said.
        verify(settings.languageHelp.text.includes("panel widget"));
        verify(settings.languageHelp.text.includes("reverblabr-ctl"));
        settings.close();
    }

    function test_theLanguagesAreFollowSystemThenEnglishThenEachCatalog() {
        const window = testCase.openSettings();
        const box = window.settingsWindow.languageBox;
        // Following the system first, saying what it gives.
        compare(box.valueAt(0), "");
        verify(LanguageSetting.systemLanguageName.length > 0);
        compare(box.textAt(0), "Follow System (" + LanguageSetting.systemLanguageName + ")");
        compare(box.valueAt(1), "en");
        compare(box.textAt(1), "English");
        // **Each catalog in its own words**, with the territory a code
        // names, in a reader's order (the words are the locale data's).
        compare(box.count, 5);
        compare(box.textAt(2), "Deutsch");
        compare(box.textAt(3), "Português (Brasil)");
        verify(box.textAt(4).includes("中文") && box.textAt(4).endsWith("(中国)"), box.textAt(4));
        compare(box.currentIndex, 0, "a first run does not follow the system");
        window.settingsWindow.close();
    }

    function test_pickingALanguageWritesItAndOffersARestart() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        verify(!settings.languageRestart.visible, "a restart offered with nothing changed");

        const box = settings.languageBox;
        const other = testCase.otherLanguage();
        box.currentIndex = testCase.languageRow(settings, other);
        box.activated(box.currentIndex);
        compare(LanguageSetting.chosen, other, "the choice was not kept");
        verify(LanguageSetting.restartNeeded);
        tryVerify(() => settings.languageRestart.visible, 1000, "no restart offered for another language");
        verify(!settings.languageRestart.text.includes("stops"), "a restart that stops nothing says it does");

        // **The language in force already needs no restart**, chosen by its
        // name rather than by following the system.
        box.currentIndex = testCase.languageRow(settings, LanguageSetting.inForce);
        box.activated(box.currentIndex);
        compare(LanguageSetting.chosen, LanguageSetting.inForce);
        tryVerify(() => !settings.languageRestart.visible, 1000, "a restart offered for the language in force");

        // Picked back, and the offer goes.
        box.currentIndex = testCase.languageRow(settings, other);
        box.activated(box.currentIndex);
        tryVerify(() => settings.languageRestart.visible, 1000);
        box.currentIndex = 0;
        box.activated(0);
        compare(LanguageSetting.chosen, "", "following the system again was not kept");
        tryVerify(() => !settings.languageRestart.visible, 1000, "the restart is still offered");
        settings.close();
    }

    function test_theWindowShowsAChoiceMadeElsewhere() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        const box = settings.languageBox;
        // Worked once, so that a binding would have been dropped by now.
        box.currentIndex = testCase.languageRow(settings, "pt_BR");
        box.activated(box.currentIndex);
        LanguageSetting.chosen = "zh_CN";
        compare(box.currentValue, "zh_CN", "the box did not follow the setting");
        LanguageSetting.chosen = "";
        compare(box.currentIndex, 0);
        settings.close();
    }

    function test_aRestartThatStopsSessionsSaysSo() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        LanguageSetting.chosen = testCase.otherLanguage();
        tryVerify(() => settings.languageRestart.visible, 1000);
        // **The engine in the window**, asked for or fallen back to: its
        // sessions stop with it.
        AppShell.engineInBackground = false;
        verify(settings.languageRestart.text.includes("Restarting stops the sessions running in this window"),
               "a restart that stops the sessions does not say so: " + settings.languageRestart.text);
        AppShell.engineInBackground = true;
        verify(!settings.languageRestart.text.includes("stops"));
        settings.close();
    }

    function test_restartNowStartsACopyThatWaitsForThisOne() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        LanguageSetting.chosen = testCase.otherLanguage();
        tryVerify(() => settings.languageRestart.visible, 1000);
        testCase.click(settings, settings.languageRestart.button);
        compare(TestRestart.count, 1, "Restart Now started nothing");
        // The program itself: here, the suite's.
        verify(TestRestart.lastProgram.endsWith("reverblabr-qml-test"), TestRestart.lastProgram);
        // **Only what chose the engine, and which process to wait for**:
        // none of the suite's own arguments.
        const restartOf = TestRestart.lastArguments.filter(argument => argument.startsWith("--restart-of="));
        compare(restartOf.length, 1, "the copy is not told which process it replaces: " + TestRestart.lastArguments);
        verify(Number(restartOf[0].split("=")[1]) > 1);
        compare(TestRestart.lastArguments.length, 1, "the copy is given more: " + TestRestart.lastArguments);
        verify(!settings.restartFailedMessage.visible);

        // **A copy that would not start** leaves this window running, and
        // says so.
        TestRestart.answer = false;
        testCase.click(settings, settings.languageRestart.button);
        compare(TestRestart.count, 2);
        tryVerify(() => settings.restartFailedMessage.visible, 1000, "a failed restart is not said");
        TestRestart.answer = true;
        testCase.click(settings, settings.languageRestart.button);
        tryVerify(() => !settings.restartFailedMessage.visible, 1000);
        settings.close();
    }

    function test_restartNowWritesDownTheSizesClosingWould() {
        // **The windows that are open keep their sizes for the copy**, as
        // closing the main window writes them down: this one and a
        // Recording Inspector.
        const recording = TestEngine.addFakeRecording("settings-restart-" + Date.now(),
                                                      "Take " + Date.now() + ".wav", 0.5);
        verify(recording !== "", "the mock could not write a recording");
        TestEngine.waitForFakeRecordings();
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        const inspector = window.openInspector(recording);
        verify(inspector !== null, "no Recording Inspector");
        settings.width = settings.minimumWidth + 23;
        settings.height = settings.minimumHeight + 19;
        inspector.width = inspector.minimumWidth + 37;
        inspector.height = inspector.minimumHeight + 11;
        tryCompare(settings, "width", settings.minimumWidth + 23);
        tryCompare(inspector, "width", inspector.minimumWidth + 37);

        LanguageSetting.chosen = testCase.otherLanguage();
        tryVerify(() => settings.languageRestart.visible, 1000);
        testCase.click(settings, settings.languageRestart.button);
        compare(TestRestart.count, 1, "Restart Now started nothing");
        compare(WindowState.windowSize(settings.stateName).width, settings.minimumWidth + 23,
                "the Settings window's size was not written down");
        compare(WindowState.windowSize(settings.stateName).height, settings.minimumHeight + 19);
        compare(WindowState.windowSize(inspector.stateName).width, inspector.minimumWidth + 37,
                "the Recording Inspector's size was not written down");
        compare(WindowState.windowSize(inspector.stateName).height, inspector.minimumHeight + 11);
        inspector.close();
        settings.close();
    }

    function test_theAppearanceIsTheMenusAppearance() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        compare(settings.darkChoice.text, "Reverblabr Dark");
        compare(settings.lightChoice.text, "Reverblabr Light");
        compare(settings.systemChoice.text, "Follow System");

        testCase.click(settings, settings.lightChoice);
        compare(Appearance.mode, Appearance.ReverblabrLight, "the window's choice did not change the appearance");
        verify(window.lightAppearanceAction.checked, "the menu does not agree");
        // **The menu's choice shows here too**, after a click here, which
        // would have dropped a binding.
        window.darkAppearanceAction.trigger();
        compare(Appearance.mode, Appearance.ReverblabrDark);
        verify(settings.darkChoice.checked, "the window does not show the menu's choice");
        verify(!settings.lightChoice.checked);
        settings.close();
    }

    function test_aControlStyleThatWaitsForARestartSaysSo() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        verify(!settings.appearanceRestart.visible, "a restart offered for the appearance the window started in");
        // **Another style than the window's**: the colours change at once,
        // the controls at the next start.
        if (Appearance.ownStyle) {
            testCase.click(settings, settings.systemChoice);
        } else {
            testCase.click(settings, settings.darkChoice);
        }
        tryVerify(() => settings.appearanceRestart.visible, 1000, "no restart offered for another control style");
        // **Back to an appearance of the window's style**, whichever of them:
        // Reverblabr Light is the same style as Reverblabr Dark.
        Appearance.mode = Appearance.ownStyle ? Appearance.ReverblabrLight : Appearance.FollowSystem;
        tryVerify(() => !settings.appearanceRestart.visible, 1000, "a restart offered within the same style");
        settings.close();
    }

    function test_autoRunIsTheMenusAutoRun() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        verify(settings.onceALoginChoice.checked, "once a login is not the default");
        compare(settings.onceALoginChoice.text, "Once per Login: Stopped Sessions Stay Stopped");
        compare(settings.everyStartChoice.text, "Every Time the Background Engine Starts");
        // **Each says what it does.**
        verify(settings.onceALoginChoice.Accessible.description.length > 0);
        verify(settings.everyStartChoice.Accessible.description.length > 0);

        testCase.click(settings, settings.everyStartChoice);
        verify(AutoRunSetting.everyEngineStart, "the window's choice did not change the setting");
        verify(window.autoRunEveryStartAction.checked, "the menu does not agree");
        window.autoRunOnceALoginAction.trigger();
        verify(!AutoRunSetting.everyEngineStart);
        verify(settings.onceALoginChoice.checked, "the window does not show the menu's choice");

        // **Where nothing would run them**, the window says so.
        verify(!settings.autoRunUnavailable.visible);
        AppShell.sandboxed = true;
        verify(settings.autoRunUnavailable.visible);
        verify(settings.autoRunUnavailable.text.includes("Flatpak"));
        AppShell.sandboxed = false;
        AppShell.engineInBackground = false;
        verify(settings.autoRunUnavailable.visible);
        verify(settings.autoRunUnavailable.text.includes("background engine"));
        settings.close();
    }

    function test_everyChoiceIsReachedWithTheKeyboard() {
        const window = testCase.openSettings();
        const settings = window.settingsWindow;
        LanguageSetting.chosen = testCase.otherLanguage();
        if (!Appearance.ownStyle) {
            // A style restart offered too, so that both buttons are on the way.
            Appearance.mode = Appearance.ReverblabrDark;
        } else {
            Appearance.mode = Appearance.FollowSystem;
        }
        tryVerify(() => settings.languageRestart.visible && settings.appearanceRestart.visible, 1000);
        settings.requestActivate();
        tryVerify(() => settings.languageBox.activeFocus, 1000, "the keyboard does not start at the first choice");
        const wanted = [settings.languageRestart.button, settings.darkChoice, settings.lightChoice,
                        settings.systemChoice, settings.appearanceRestart.button, settings.onceALoginChoice,
                        settings.everyStartChoice];
        const reached = [];
        for (let press = 0; press < 20 && reached.length < wanted.length; ++press) {
            keyClick(Qt.Key_Tab);
            const item = settings.activeFocusItem;
            if (wanted.indexOf(item) >= 0 && reached.indexOf(item) < 0) {
                reached.push(item);
            }
        }
        compare(reached.length, wanted.length, "Tab does not reach every choice");
        // **What the keyboard reached is in view**, the page scrolled to it.
        const flickable = settings.scroll.contentItem;
        const last = settings.everyStartChoice;
        const y = last.mapToItem(flickable, 0, 0).y;
        verify(y >= 0 && y + last.height <= flickable.height + 1, "the last choice is out of sight: " + y);
        settings.close();
    }
}
