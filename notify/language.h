// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace rb::notify
{

/// **The language the person chose for Reverblabr** (ADR-0036): one choice
/// in the application's settings file, `reverblabrrc` (a KConfig file), which
/// the window writes and every process of Reverblabr that speaks reads. Words
/// in the file: a catalog's language code as its folder is named (`de`,
/// `pt_BR`, `sr@latin`), `en` for English, and nothing at all to follow the
/// system.
namespace languageSetting
{
inline constexpr auto group = "General";
inline constexpr auto key = "Language";
/// English, the language Reverblabr is written in, which has no catalog.
inline constexpr auto english = "en";
} // namespace languageSetting

/// `$XDG_CONFIG_HOME/reverblabrrc`, the window's settings file. **Built from
/// the generic configuration folder, not the application's**, so it can be
/// asked for before there is an application to name it: the window reads it
/// before Qt starts. In a Flatpak that folder is the app's own, for every
/// process in the sandbox alike.
[[nodiscard]] QString settingsPath();

/// Whether \a word is a language code as a catalog's folder is named: a
/// language, perhaps a territory, perhaps a modifier (`de`, `pt_BR`,
/// `es_419`, `sr@latin`). **Nothing else gets into LANGUAGE**, which is a
/// list: a value with a colon in it would be several languages nobody chose.
[[nodiscard]] bool isLanguageCode(const QString &word);

/// Whether Reverblabr's catalog, `reverblabr.mo`, is installed for \a code:
/// in the `locale` folder of any of the data folders, where KI18n looks too.
[[nodiscard]] bool hasCatalog(const QString &code);

/// **What \a settingsPath says the language is**: a code there is a catalog
/// for, `en`, or empty to follow the system. Empty too for a file or a key
/// that is not there, for a word that is not a language code, and for a
/// language whose catalog has gone, so that every process falls back to the
/// system's language alike rather than each in its own way.
///
/// **Read without KConfig**: the daemon has no KDE Frameworks (ADR-0017),
/// and the window reads it before KConfig could be told which file is the
/// application's. Read afresh each time, for the daemon to follow it.
[[nodiscard]] QString chosenLanguage(const QString &settingsPath);

/// **The one variable the choice changes**: `LANGUAGE`, which gettext, KI18n
/// and Qt all read for the language of the words, ahead of `LC_ALL`,
/// `LC_MESSAGES` and `LANG`. Those, and so every number, date and unit
/// format, stay the system's.
///
/// For the whole process, so on the main thread only: setting a variable
/// while another thread reads the environment is a race the C library does
/// not guard against.
class ProcessLanguage
{
public:
    /// **Before Qt starts, and before any thread**: remembers what `LANGUAGE`
    /// was (or that it was not set) and sets it to \a code when one is
    /// chosen. Otherwise it stays as it was, and is made to exist, empty
    /// (which gettext, KI18n and Qt all read as not set), so that a later
    /// change, by follow() or by KI18n's own lookups, replaces a value rather
    /// than adding one to the environment while other threads read it.
    /// Called once by each program; a test may call it again.
    static void applyAtStart(const QString &code);

    /// **The language changed while the process runs** (the daemon,
    /// ADR-0036): `LANGUAGE` becomes \a code, or what it was at start for
    /// empty, and gettext is told to forget what it looked up. Answers
    /// whether anything changed.
    static bool follow(const QString &code);

    /// The language applyAtStart() was given, as chosenLanguage() said it:
    /// empty when the process follows the system.
    [[nodiscard]] static QString atStart();

    /// \a environment with `LANGUAGE` as it was before applyAtStart(): **what
    /// a program this process starts is given**, so that one that speaks
    /// reads the person's choice for itself, and one that follows the system
    /// is not handed Reverblabr's language as the system's.
    [[nodiscard]] static QProcessEnvironment restored(QProcessEnvironment environment);

    /// This process's environment as restored() gives it.
    [[nodiscard]] static QProcessEnvironment originalEnvironment();
};

} // namespace rb::notify
