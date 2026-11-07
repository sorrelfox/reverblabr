<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0036: The language is Reverblabr's own setting: LANGUAGE set for the window before Qt starts and taken at the next start, given back as it was to what Reverblabr starts, followed by the daemon as it changes; what the desktop shows follows the desktop

- **Status:** Accepted
- **Date:** 2026-11-07

## Context

Reverblabr's words are translated four ways (docs/i18n.md): the window's through KI18n, the notifications' and KRunner's through plain gettext in `notify/` (in the window and in the daemon, which has no KDE Frameworks, ADR-0017), `reverblabr-ctl`'s through gettext of its own, and the panel widget's through Plasma. What the desktop shows before Reverblabr runs (the desktop entry, the notification events in System Settings, KRunner's entry in its settings, the AppStream data) is translated into those files when they are installed. All of it followed the desktop's language. A person should be able to choose another for Reverblabr: a translation they would rather read, or English for the words they know from other audio programs.

What there is to work with:

- **Every lookup ends in gettext**, which takes the language from `LANGUAGE`, ahead of `LC_ALL`, `LC_MESSAGES` and `LANG`, and ignores it only in the C locale. KI18n reads its list of languages from the same variables, once, the first time anything is translated. Qt's own catalogs, and Kirigami's, are loaded as the application starts, for the languages `QLocale` says the interface is in, which follow `LANGUAGE` too.
- **Formats do not follow `LANGUAGE`.** Measured with Qt 6.11: `LANGUAGE=de` on an `en_US.UTF-8` system gives Qt's German ("Strg+N") and KI18n's German, and still "1,234.5" and "Saturday, November 7, 2026"; on `fr_FR.UTF-8`, "1 234,5". Numbers, dates and units come from `LC_NUMERIC`, `LC_TIME` and the rest.
- **Much of what the window says is worked out once**, in C++: the block catalogue's names, the menus' actions, the error messages a model holds. KI18n has no way to say them again in another language while the window is open, and neither has Qt for the catalogs it loaded.
- **The control style cannot change in an open window either** (ADR-0012): picking another appearance already changed the colours at once and the controls only at the next start, with nothing to start it.
- **The daemon outlives every window**, for days, and starts without one: at login, for a timer, for a KRunner query. Its words are gettext's, which caches what it looked up without regard to `LANGUAGE`; glibc's `_nl_msg_cat_cntr` makes it look again, and KI18n moves it whenever it changes `LANGUAGE` for a lookup of its own.
- **What a process starts inherits its environment**: a window restarted by itself, the daemon a Flatpak's window starts (ADR-0034), the window KRunner's Open starts from the daemon (ADR-0023), and what KIO and Qt open for the window (Open With…, the handbook, a link), whose launchers take no environment of ours.
- **KDE has a per-application language** for KXmlGui programs, Help → Configure Language: a widgets dialog that keeps the choice in `klanguageoverridesrc` under the program's name and puts it in front of `LANGUAGE` from inside `QCoreApplication`'s constructor, for the next start.

## Decision

**One setting: `[General] Language=` in `reverblabrrc`**, the application's settings file: a catalog's language code as its folder is named (`de`, `pt_BR`, `sr@latin`), `en` for English, or nothing to follow the system, which is the default. The window writes it with KConfig (`rb::ui::LanguageSetting`). Every process reads it with `rb::notify::chosenLanguage()`, a reader of its own in the KDE-free `notify/`: not KConfig, which the daemon does not have, and not QSettings, which kept what it had read of the file while its size and time looked the same and missed a choice as long as the last. A code with no catalog installed, or a word that is no code, is following the system, in every process alike.

**The window takes it once, before Qt starts.** `main()` reads it before `QApplication` exists, when there is no application name to find the file by, from the generic configuration folder (the app's own in a Flatpak), and sets `LANGUAGE` to the chosen code for the whole process. KI18n, Qt's and Kirigami's catalogs and `notify/`'s gettext then all speak it. Only `LANGUAGE` is touched, never `LANG` or `LC_*`, so every format stays the system's. A change takes effect at the next start: no part of the window switches while it is open, so no window speaks two languages.

**Restart Now**, in the Settings window, wherever a choice waits for a start: a language other than the one in force, or an appearance whose control style is not the one the window started with. It starts a copy of the program on its own, with the environment the window was started with and only the options that chose the engine (`--mock`, `--in-process`, `--daemon`; never a launch's request, such as a session to open), plus a hidden `--restart-of=<pid>`. Then the window writes down what closing writes and quits as the tray's Quit does. **The copy waits for the old process before it asks for Reverblabr's name on the bus**: it looks at `/proc/<pid>/stat` about every 50 ms, for at most 10 seconds, while that pid is a process of its own name that has not exited; it never signals it. The name, the tray icon and the global shortcuts are free by the time it asks, so there is still one Reverblabr a desktop. A window running its own engine, asked for or fallen back to, stops its sessions with it, and the row says so beside the button.

**What Reverblabr starts gets `LANGUAGE` back as it was** (`rb::notify::ProcessLanguage` remembers it, or that it was not set): Restart Now's copy, the Flatpak's daemon (`rb::bus::detachedStarter()` with that environment), and the window the daemon starts for KRunner's Open. Each reads the choice for itself, and none takes Reverblabr's language for the desktop's. **What Reverblabr opens for the person inherits its language**, as it does from a KXmlGui program with a language of its own: what KIO and Qt open for the window (Open With…, the handbook, a link), and the player the daemon opens a finished recording in from its notification (`xdg-open`). So do the plugin and model hosts, which say nothing of their own; a plugin's own window is part of the session it is in.

**The daemon follows the choice as it changes.** As it starts, before `QCoreApplication` and before any other thread, it sets `LANGUAGE` as the window does, and makes the variable exist (empty, which reads as not set) when it did not, so that a later change replaces a value rather than adding one to the environment while other threads read it. Then `rb::daemon::LanguageWatch` watches the file and its folder (KConfig saves by renaming a new file over the old one), reads the choice a moment after anything there changes, and on the main thread sets `LANGUAGE` to the new code, or to what it was for the system's, and moves `_nl_msg_cat_cntr` where glibc has it. Its next notification and its next KRunner answer are in the new language.

**What the desktop shows follows the desktop**: the panel widget, which Plasma translates in the desktop's language (ADR-0024); `reverblabr-ctl`, which as a command-line tool follows the environment it is run in; and the desktop entry, the notification events, KRunner's entry and the AppStream data, which other programs show. The Settings window says so under the choice.

**The choices**: Follow System first, naming the language it gives (the first of the desktop's, in the order KI18n reads them: `LANGUAGE`'s list, then `LC_ALL`, `LC_MESSAGES` and `LANG`, each also without its territory, that has a catalog; English when none has, when English comes first, and in the C locale); then English; then every language with an installed `reverblabr` catalog, as KI18n finds them in the data folders, so a build tree's own too. Each is named in its own language, from `QLocale`: the name most of the language's locales give it (CLDR names some by their variety, "español de España"), with the territory a code names ("Português (Brasil)"), capitalised as that language capitalises, and sorted by the collation of the locale the window runs in.

**The Settings window** (Settings → Configure Reverblabr…, KDE's place and key for it) holds the language, the appearance and when Auto Run runs, a section each on a page that scrolls, through the same singletons as the menus, which keep their entries.

## Consequences

- Reverblabr can speak another language than the desktop, with the desktop's formats.
- A choice of language takes a restart of the window, offered where it is made; the daemon's words change at once, and a session running in the daemon is not touched by either.
- A KDE program opened from Reverblabr (Open With…, the handbook in KHelpCenter) comes up in Reverblabr's language: the handbook in the language chosen, where it is translated.
- A message the chosen catalog lacks is in English where gettext looks it up, and among the window's KI18n words in the next of the desktop's languages that has it, then English.
- In the C locale no choice has any effect, since gettext translates nothing there; a Plasma session always has a locale.
- `reverblabrrc`'s `[General]` group is read by every process of Reverblabr, and its words are the code's: a later setting of the window there is read by the daemon as it is written.
- The global shortcuts are listed in System Settings under the names the window gives them as it registers them, so in the language chosen, from the window's next start.
- The Settings window is where settings still to come go; the menus' Appearance and Auto Run stay, as quicker ways to the same two.

## Alternatives considered

- **Switching the window's language while it is open**, with `KLocalizedString::setLanguages()` and a language-change event. What C++ worked out once would stay in the old language beside the new, and Qt's and Kirigami's catalogs would have to be loaded again; a window in two languages is worse than a restart.
- **KXmlGui's Configure Language and `klanguageoverridesrc`.** It brings a widgets library in for one dialog, keeps the choice in a file of its own under the program's name, which the daemon would have to read as well, and applies it from inside `QCoreApplication`'s constructor, after Qt has read its locale.
- **Setting `LANG` or `LC_MESSAGES`.** `LANG` changes every format; `LC_MESSAGES` needs the chosen language's locale to be installed, where `LANGUAGE` needs only some locale that is not C.
- **Telling the daemon over `Engine1`.** A public method for what the daemon can read from the file, and a daemon that is not running would still have to read it as it starts.
- **Restarting the daemon on a change.** The sessions it runs would stop for a choice of words.
- **Letting what Reverblabr starts inherit its language.** Restart Now's copy and the Flatpak's daemon would take Reverblabr's language for the desktop's, and following the system again would never go back to it.
