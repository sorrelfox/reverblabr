// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **Kirigami's colours, under the Reverblabr control style**.
//
// Kirigami asks a plugin named after the Qt Quick Controls style for its
// colours, fonts and units. The desktop style has one that reads the
// user's colour scheme; a style without one gets Kirigami's built-in theme,
// which is Breeze's light colours whatever the window looks like — a light
// dialog, a light separator and dark icons in a dark window. This is the
// plugin for the "Reverblabr" style: every colour set Kirigami knows is
// made from the design's tokens, which the application hands over through
// its own object (designpalette.h).

#include "designpalette.h"

#include <Kirigami/Platform/PlatformPluginFactory>
#include <Kirigami/Platform/PlatformTheme>
#include <Kirigami/Platform/Units>
#include <QCoreApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace
{

using Kirigami::Platform::PlatformTheme;

/// Tells every theme that the colours or the font have changed. One object
/// watching the application, rather than every theme watching it.
class Watcher : public QObject
{
    Q_OBJECT

public:
    static Watcher *instance()
    {
        static Watcher *const watcher = [] {
            auto *const created = new Watcher(QCoreApplication::instance());
            QCoreApplication::instance()->installEventFilter(created);
            return created;
        }();
        return watcher;
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == QCoreApplication::instance()
            && (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ApplicationFontChange)) {
            Q_EMIT changed();
        }
        return false;
    }

Q_SIGNALS:
    void changed();

private:
    using QObject::QObject;
};

/// The design's colours by their token names, or from the palette where
/// the application has not handed them over.
class Colours
{
public:
    Colours()
        : m_palette(QGuiApplication::palette())
        , m_tokens(QCoreApplication::instance()->property(rb::ui::kDesignColorsProperty).toMap())
    {
    }

    [[nodiscard]] QColor get(const QString &token, QPalette::ColorRole fallback) const
    {
        const QColor colour = m_tokens.value(token).value<QColor>();
        return colour.isValid() ? colour : m_palette.color(QPalette::Active, fallback);
    }

    [[nodiscard]] QColor get(const QString &token, const QColor &fallback) const
    {
        const QColor colour = m_tokens.value(token).value<QColor>();
        return colour.isValid() ? colour : fallback;
    }

    /// A family the application named, or nothing.
    [[nodiscard]] QString family(const QString &name) const
    {
        return m_tokens.value(name).toString();
    }

private:
    QPalette m_palette;
    QVariantMap m_tokens;
};

class Theme : public PlatformTheme
{
    Q_OBJECT

public:
    explicit Theme(QObject *parent)
        : PlatformTheme(parent)
    {
        setSupportsIconColoring(true);
        connect(Watcher::instance(), &Watcher::changed, this, &Theme::sync);
        sync();
    }

protected:
    bool event(QEvent *event) override
    {
        using namespace Kirigami::Platform::PlatformThemeEvents;
        if (event->type() == DataChangedEvent::type || event->type() == ColorSetChangedEvent::type
            || event->type() == ColorGroupChangedEvent::type) {
            sync();
        }
        return PlatformTheme::event(event);
    }

private:
    void sync()
    {
        Kirigami::Platform::PlatformThemeChangeTracker tracker(this);
        const Colours c;

        const QColor text = c.get(u"text"_s, QPalette::WindowText);
        const QColor muted = c.get(u"muted"_s, QPalette::PlaceholderText);
        const QColor accent = c.get(u"accent"_s, QPalette::Highlight);
        const QColor onAccent = c.get(u"textOnAccent"_s, QPalette::HighlightedText);
        const QColor window = c.get(u"window"_s, QPalette::Window);
        const QColor surface = c.get(u"surface"_s, QPalette::Base);
        const QColor surface2 = c.get(u"surface2"_s, QPalette::AlternateBase);
        const QColor field = c.get(u"field"_s, QPalette::Base);

        // **Each set is where the design puts that kind of thing**: a
        // window on the window's colour, a button on the raised surface, a
        // selection in the accent — and a view on the surface, because what
        // Kirigami draws in that set is a dialog and what it holds, which the
        // prototype draws as its popover. Fields are the style's own,
        // on the field colour.
        QColor background;
        QColor alternate;
        QColor foreground = text;
        switch (colorSet()) {
        case PlatformTheme::View:
            background = surface;
            alternate = field;
            break;
        case PlatformTheme::Button:
        case PlatformTheme::Tooltip:
            background = surface2;
            alternate = surface;
            break;
        case PlatformTheme::Selection:
            background = accent;
            alternate = accent;
            foreground = onAccent;
            break;
        case PlatformTheme::Window:
        case PlatformTheme::Header:
        case PlatformTheme::Complementary:
        default:
            background = window;
            alternate = surface2;
            break;
        }
        const bool disabled = colorGroup() == PlatformTheme::Disabled;

        setTextColor(disabled ? muted : foreground);
        setDisabledTextColor(muted);
        setHighlightedTextColor(onAccent);
        setActiveTextColor(accent);
        setLinkColor(accent);
        setVisitedLinkColor(c.get(u"linkHover"_s, QPalette::LinkVisited));
        setNegativeTextColor(c.get(u"danger"_s, QColor(u"#da4453"_s)));
        setNeutralTextColor(c.get(u"warning"_s, QColor(u"#f67400"_s)));
        setPositiveTextColor(c.get(u"ok"_s, QColor(u"#27ae60"_s)));

        setBackgroundColor(background);
        setAlternateBackgroundColor(alternate);
        setHighlightColor(accent);
        setActiveBackgroundColor(accent);
        setLinkBackgroundColor(accent);
        setVisitedLinkBackgroundColor(accent);
        setNegativeBackgroundColor(c.get(u"danger"_s, QColor(u"#da4453"_s)));
        setNeutralBackgroundColor(c.get(u"warning"_s, QColor(u"#f67400"_s)));
        setPositiveBackgroundColor(c.get(u"ok"_s, QColor(u"#27ae60"_s)));

        setFocusColor(accent);
        setHoverColor(accent);

        // **The type is the application's**, which the appearance has
        // already made the design's body face.
        const QFont body = QGuiApplication::font();
        setDefaultFont(body);
        QFont small = body;
        // The prototype's caption against its body: 11.5 to 12.5 pixels.
        small.setPointSizeF(body.pointSizeF() * 11.5 / 12.5);
        setSmallFont(small);
        const QString mono = c.family(u"monoFamily"_s);
        QFont fixed = mono.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(mono);
        fixed.setPointSizeF(body.pointSizeF());
        setFixedWidthFont(fixed);
    }
};

/// Kirigami's own measures: the spacings follow the font, which is already
/// the design's, and are left alone. **The corner is the design's popover
/// corner**, since what Kirigami rounds by it — a dialog, an inline
/// message — is drawn as the prototype draws its settings panel. The
/// constructor is only reachable from a subclass.
class Units : public Kirigami::Platform::Units
{
public:
    explicit Units(QObject *parent)
        : Kirigami::Platform::Units(parent)
    {
        connect(Watcher::instance(), &Watcher::changed, this, &Units::sync);
        sync();
    }

private:
    void sync()
    {
        const QVariant radius =
            QCoreApplication::instance()->property(rb::ui::kDesignColorsProperty).toMap().value(u"cornerRadius"_s);
        if (radius.isValid()) {
            setcornerRadius(radius.toReal());
        }
    }
};

class Plugin : public Kirigami::Platform::PlatformPluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PlatformPluginFactory_iid)
    Q_INTERFACES(Kirigami::Platform::PlatformPluginFactory)

public:
    using PlatformPluginFactory::PlatformPluginFactory;

    PlatformTheme *createPlatformTheme(QObject *parent) override
    {
        return new Theme(parent);
    }

    Kirigami::Platform::Units *createUnits(QObject *parent) override
    {
        return new Units(parent);
    }
};

} // namespace

#include "reverblabrplatform.moc"
