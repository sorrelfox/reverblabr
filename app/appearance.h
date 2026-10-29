// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <KSharedConfig>
#include <QFont>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

namespace rb::ui
{

/// Which look the window has ([ADR-0012]): the design prototype's own
/// palettes, type and metrics, dark or light, or the desktop's.
///
/// **One choice, read by everything that draws.** `AppColors`, `AppFonts`
/// and `AppMetrics` are where the values live; this is only which set of
/// them is in force, remembered between runs and applied while the window
/// is open wherever Qt allows it.
///
/// **In the settings file, not the state file.** Unlike a window's size,
/// this is something a person chose, and belongs where a person's choices
/// go (`reverblabrrc`).
class Appearance : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    /// Whether the desktop decides: the user's colour scheme, font and
    /// spacing, exactly as the application looked before it had a look of
    /// its own.
    Q_PROPERTY(bool followsSystem READ followsSystem NOTIFY modeChanged)
    /// Whether the prototype's dark palette is in force. Meaningless while
    /// following the system, where the colour scheme says.
    Q_PROPERTY(bool dark READ isDark NOTIFY modeChanged)
    /// The bundled faces' family names, as the font database has them.
    Q_PROPERTY(QString sansFamily READ sansFamily CONSTANT)
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)
    /// Whether the controls are drawn by the Reverblabr style, which
    /// was settled when the application started.
    Q_PROPERTY(bool ownStyle READ ownStyle CONSTANT)

public:
    enum Mode {
        ReverblabrDark,
        ReverblabrLight,
        FollowSystem,
    };
    Q_ENUM(Mode)

    /// The one instance, which QML is handed too: the application font is
    /// process-wide, so two objects each setting it would be two opinions.
    static Appearance *instance();
    static Appearance *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    [[nodiscard]] Mode mode() const;
    void setMode(Mode mode);
    [[nodiscard]] bool followsSystem() const;
    [[nodiscard]] bool isDark() const;

    [[nodiscard]] static QString sansFamily();
    [[nodiscard]] static QString monoFamily();

    /// What was chosen, read straight from the settings file: for the start
    /// of the application, before there is a window, to pick the control
    /// style by.
    [[nodiscard]] static Mode storedMode();

    /// **The prototype measures type in CSS pixels, Qt in points.** A pixel
    /// size can only be whole in a QFont, and the prototype's scale has
    /// halves in it (12.5, 11.5, 10.5, 9.5), so a size goes to Qt as the
    /// point size that comes to the same pixels at the DPI Qt lays text out
    /// at.
    [[nodiscard]] Q_INVOKABLE static qreal points(qreal pixels);

    /// The body text of the Reverblabr appearance: Noto Sans at 12.5 px.
    [[nodiscard]] static QFont bodyFont();

    /// The name of the control style the application draws with in the
    /// Reverblabr appearance: Qt Quick Controls finds it as the QML
    /// module of that name.
    static constexpr QLatin1StringView styleName{"Reverblabr"};

    /// The control style for a mode, chosen once at start: Qt
    /// cannot change the style of a window that is already open.
    [[nodiscard]] static QString styleFor(Mode mode);

    /// Picks the control style for a mode, unless the environment already
    /// names one (`QT_QUICK_CONTROLS_STYLE`), and sets text to be drawn as
    /// that style expects. For the start of the application, before there
    /// is a QML engine.
    static void useStyleFor(Mode mode);

    /// Whether the application is drawing with its own control style: the
    /// choice made at start, which a change of appearance while the window
    /// is open does not undo.
    [[nodiscard]] static bool ownStyle();

    /// **The design's colours, handed to what is not QML**: the
    /// application palette, which Qt's Basic controls draw with wherever
    /// the Reverblabr style has no control of its own yet, and Kirigami's
    /// colours through the platform plugin (designpalette.h). `AppColors`
    /// calls this whenever its colours change, with its tokens by name, or
    /// with nothing while the system decides.
    ///
    /// **Only under the Reverblabr style.** The desktop style reads the
    /// colour scheme itself, and *Follow system* must stay exactly the
    /// desktop's; a window that started in it keeps its palette until it
    /// is restarted in another appearance.
    Q_INVOKABLE static void setDesignColors(const QVariantMap &colours);

    /// **An action's key, as a menu row writes it**: the platform's
    /// own words for it ("Ctrl+N"), or nothing. \a shortcut is what an
    /// action holds — a key sequence in words, or one of the standard keys,
    /// of which the first binding is the one shown.
    [[nodiscard]] Q_INVOKABLE static QString shortcutText(const QVariant &shortcut);

    /// Puts the bundled faces in the font database. Idempotent; the
    /// application and the tests both call it before anything draws.
    static void registerFonts();

    /// Where to read and write. A test gives its own, so that running the
    /// tests does not reach into whoever is running them.
    static void setConfig(const KSharedConfig::Ptr &config);

Q_SIGNALS:
    void modeChanged();

private:
    explicit Appearance(QObject *parent = nullptr);

    /// The application font for the mode: the bundled face, or whatever the
    /// desktop gave the application before we changed it.
    void applyFont();

    Mode m_mode;
    QFont m_systemFont;
    bool m_fontChanged = false;
};

} // namespace rb::ui
