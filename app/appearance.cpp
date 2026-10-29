// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "appearance.h"

#include "designpalette.h"

#include <KConfigGroup>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QPalette>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

Q_LOGGING_CATEGORY(lcAppearance, "reverblabr.appearance")

KSharedConfig::Ptr g_config;

constexpr QLatin1StringView kGroup{"Appearance"};
constexpr QLatin1StringView kMode{"Mode"};

// **Words in the file, not the enum's numbers**, so that reordering the
// enum cannot turn somebody's dark window light.
constexpr QLatin1StringView kDark{"reverblabr-dark"};
constexpr QLatin1StringView kLight{"reverblabr-light"};
constexpr QLatin1StringView kSystem{"system"};

// The prototype's body text, in CSS pixels.
constexpr qreal kBodyPixels = 12.5;

constexpr QLatin1StringView kSans{"Noto Sans"};
constexpr QLatin1StringView kMono{"JetBrains Mono"};

KConfigGroup group()
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openConfig();
    return KConfigGroup(config, kGroup);
}

Appearance::Mode modeFromWord(const QString &word)
{
    if (word == kLight) {
        return Appearance::ReverblabrLight;
    }
    if (word == kSystem) {
        return Appearance::FollowSystem;
    }
    if (word == kDark) {
        return Appearance::ReverblabrDark;
    }
    // Nothing written, or a word from a version we do not know: the default,
    // **the design's dark look** ([ADR-0012]). It was *Follow system* while
    // any of the window was still the desktop's: a menu or a tooltip
    // drawn with the desktop's palette opened light in a dark window. Now
    // every control, popup and message in the window is the style's.
    return Appearance::ReverblabrDark;
}

QLatin1StringView wordFor(Appearance::Mode mode)
{
    switch (mode) {
    case Appearance::ReverblabrLight:
        return kLight;
    case Appearance::FollowSystem:
        return kSystem;
    case Appearance::ReverblabrDark:
        break;
    }
    return kDark;
}

} // namespace

Appearance::Appearance(QObject *parent)
    : QObject(parent)
    , m_mode(storedMode())
    , m_systemFont(QGuiApplication::font())
{
    registerFonts();
    applyFont();
}

Appearance *Appearance::instance()
{
    // **Never deleted.** QML holds it for as long as any engine lives, the
    // application font it set outlives every engine, and an object parented
    // to the application would leave this pointer dangling for a process
    // that makes a second one (a test).
    static Appearance *const appearance = new Appearance;
    return appearance;
}

Appearance *Appearance::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    Appearance *const appearance = instance();
    QJSEngine::setObjectOwnership(appearance, QJSEngine::CppOwnership);
    return appearance;
}

Appearance::Mode Appearance::mode() const
{
    return m_mode;
}

void Appearance::setMode(Mode mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    KConfigGroup settings = group();
    settings.writeEntry(kMode, QString(wordFor(mode)));
    settings.sync();
    applyFont();
    Q_EMIT modeChanged();
}

bool Appearance::followsSystem() const
{
    return m_mode == FollowSystem;
}

bool Appearance::isDark() const
{
    return m_mode == ReverblabrDark;
}

QString Appearance::sansFamily()
{
    return kSans;
}

QString Appearance::monoFamily()
{
    return kMono;
}

Appearance::Mode Appearance::storedMode()
{
    return modeFromWord(group().readEntry(kMode, QString{}));
}

qreal Appearance::points(qreal pixels)
{
    // The DPI Qt converts a point size at: the primary screen's logical
    // DPI, rounded, as qt_defaultDpiY() has it. 96 on Wayland and offscreen.
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? qRound(screen->logicalDotsPerInchY()) : 96.0;
    return pixels * 72.0 / dpi;
}

QFont Appearance::bodyFont()
{
    QFont font(kSans);
    font.setPointSizeF(points(kBodyPixels));
    return font;
}

QString Appearance::styleFor(Mode mode)
{
    return mode == FollowSystem ? u"org.kde.desktop"_s : QString(styleName);
}

void Appearance::useStyleFor(Mode mode)
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(styleFor(mode));
    }
    if (!ownStyle()) {
        return;
    }
    // **Text drawn by the font engine, hinted**, as a browser draws the
    // prototype's. Qt Quick's own text renderer sets the same faces softer;
    // against the prototype that alone cost every tile almost a point
    // and the panels three. Not at a fractional scale, where hinting to
    // whole pixels puts glyphs in the wrong places.
    const qreal ratio = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    if (qFuzzyCompare(ratio, std::round(ratio))) {
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    }
}

bool Appearance::ownStyle()
{
    return QQuickStyle::name() == styleName;
}

void Appearance::setDesignColors(const QVariantMap &colours)
{
    if (!ownStyle() || !qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }
    // The tokens first: the palette's change is what has Kirigami read them.
    QCoreApplication::instance()->setProperty(kDesignColorsProperty, colours);
    if (colours.isEmpty()) {
        // **Back to the desktop's**: a palette that sets nothing resolves
        // every role against the platform's own.
        QGuiApplication::setPalette(QPalette());
        return;
    }
    const auto colour = [&colours](QLatin1StringView name) { return colours.value(name).value<QColor>(); };
    // **The Basic style's roles, in the design's colours.** Basic fills a
    // chosen or filled part (a slider's travel, a progress bar, a pressed
    // scroll bar) with `dark`, so that is the accent; `mid` is a scroll
    // bar's handle, the wire's grey.
    QPalette palette;
    palette.setColor(QPalette::Window, colour("window"_L1));
    palette.setColor(QPalette::WindowText, colour("text"_L1));
    palette.setColor(QPalette::Base, colour("field"_L1));
    palette.setColor(QPalette::AlternateBase, colour("surface2"_L1));
    palette.setColor(QPalette::ToolTipBase, colour("surface2"_L1));
    palette.setColor(QPalette::ToolTipText, colour("text"_L1));
    palette.setColor(QPalette::PlaceholderText, colour("muted"_L1));
    palette.setColor(QPalette::Text, colour("text"_L1));
    palette.setColor(QPalette::Button, colour("surface2"_L1));
    palette.setColor(QPalette::ButtonText, colour("text"_L1));
    palette.setColor(QPalette::BrightText, colour("textOnAccent"_L1));
    palette.setColor(QPalette::Light, colour("surface2"_L1));
    palette.setColor(QPalette::Midlight, colour("well"_L1));
    palette.setColor(QPalette::Mid, colour("wire"_L1));
    palette.setColor(QPalette::Dark, colour("accent"_L1));
    palette.setColor(QPalette::Shadow, colour("shadow"_L1));
    palette.setColor(QPalette::Highlight, colour("accent"_L1));
    palette.setColor(QPalette::HighlightedText, colour("textOnAccent"_L1));
    palette.setColor(QPalette::Link, colour("accent"_L1));
    palette.setColor(QPalette::LinkVisited, colour("linkHover"_L1));
    palette.setColor(QPalette::Accent, colour("accent"_L1));
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, colour("muted"_L1));
    }
    QGuiApplication::setPalette(palette);
}

QString Appearance::shortcutText(const QVariant &shortcut)
{
    // **A standard key arrives as its number**, and is written as its
    // first binding on this platform: Zoom In is Ctrl++ and Ctrl+=, and a
    // menu has room for one.
    if (shortcut.typeId() == QMetaType::Int || shortcut.typeId() == QMetaType::UInt) {
        const QList<QKeySequence> bindings =
            QKeySequence::keyBindings(static_cast<QKeySequence::StandardKey>(shortcut.toInt()));
        return bindings.isEmpty() ? QString() : bindings.constFirst().toString(QKeySequence::NativeText);
    }
    if (shortcut.canConvert<QKeySequence>()) {
        return shortcut.value<QKeySequence>().toString(QKeySequence::NativeText);
    }
    return QKeySequence(shortcut.toString()).toString(QKeySequence::NativeText);
}

void Appearance::registerFonts()
{
    static const bool registered = [] {
        static constexpr std::array files{
            "NotoSans-Regular.ttf",
            "NotoSans-Medium.ttf",
            "NotoSans-SemiBold.ttf",
            "NotoSans-Bold.ttf",
            "JetBrainsMono-Medium.ttf",
            "JetBrainsMono-SemiBold.ttf",
        };
        bool all = true;
        for (const char *file : files) {
            const QString path = u":/qt/qml/io/github/reverblabr/fonts/"_s + QLatin1StringView(file);
            if (QFontDatabase::addApplicationFont(path) < 0) {
                // Not fatal: the window draws in whatever the font database
                // substitutes, which is only wrong, not broken.
                qCWarning(lcAppearance) << "could not load the bundled font" << path;
                all = false;
            }
        }
        return all;
    }();
    Q_UNUSED(registered)
}

void Appearance::setConfig(const KSharedConfig::Ptr &config)
{
    g_config = config;
}

void Appearance::applyFont()
{
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }
    if (m_mode == FollowSystem) {
        // **Only undo what we did.** An application that was never given
        // our font keeps the desktop's untouched, so that its font keeps
        // following the desktop's settings as they change.
        if (m_fontChanged) {
            QGuiApplication::setFont(m_systemFont);
            m_fontChanged = false;
        }
        return;
    }
    QGuiApplication::setFont(bodyFont());
    m_fontChanged = true;
}

} // namespace rb::ui
