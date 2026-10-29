// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Which look the window has: the choice, where it is remembered, and
// the two bundled faces the Reverblabr appearance draws with.

#include "appearance.h"

#include <KConfigGroup>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// One application for the whole run: a font database needs one, and a
/// process can only make it once. **Never destroyed**: a static one would be
/// torn down after the Qt globals it depends on, and fault on the way out.
QGuiApplication &application()
{
    static int argc = 1;
    static char name[] = "reverblabr-app-test";
    static char *argv[] = {name, nullptr};
    static QGuiApplication *const app = [] {
        // Offscreen, so the test needs no display.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return new QGuiApplication(argc, argv);
    }();
    return *app;
}

/// A settings file of the test's own, so that running the tests does not
/// reach into whoever is running them.
class SettingsFile
{
public:
    SettingsFile()
    {
        EXPECT_TRUE(m_directory.isValid());
        m_config = KSharedConfig::openConfig(m_directory.filePath(u"reverblabrrc"_s), KConfig::SimpleConfig);
        ui::Appearance::setConfig(m_config);
    }

    ~SettingsFile()
    {
        ui::Appearance::setConfig({});
    }

    SettingsFile(const SettingsFile &) = delete;
    SettingsFile &operator=(const SettingsFile &) = delete;

    [[nodiscard]] QString written() const
    {
        m_config->reparseConfiguration();
        return m_config->group(u"Appearance"_s).readEntry(u"Mode"_s, QString{});
    }

    void write(const QString &word)
    {
        KConfigGroup group = m_config->group(u"Appearance"_s);
        group.writeEntry(u"Mode"_s, word);
        group.sync();
    }

private:
    QTemporaryDir m_directory;
    KSharedConfig::Ptr m_config;
};

} // namespace

TEST(AppearanceTest, TheDefaultIsTheDesignsDarkLook)
{
    // A first run, and a file from a version with a word this one does not
    // know, both open in the design's dark look ([ADR-0012]) — now that the
    // menus and tooltips are the style's too, and a dark window no
    // longer opens the desktop's light menus.
    SettingsFile file;
    EXPECT_EQ(ui::Appearance::storedMode(), ui::Appearance::ReverblabrDark);
    file.write(u"something-later"_s);
    EXPECT_EQ(ui::Appearance::storedMode(), ui::Appearance::ReverblabrDark);
    // Somebody who chose the desktop's keeps it.
    file.write(u"system"_s);
    EXPECT_EQ(ui::Appearance::storedMode(), ui::Appearance::FollowSystem);
}

TEST(AppearanceTest, TheChoiceIsRememberedAsAWord)
{
    // **Words, not the enum's numbers**, so reordering the enum cannot turn
    // somebody's window another colour.
    application();
    SettingsFile file;
    ui::Appearance *appearance = ui::Appearance::instance();
    // From a known start: the one instance may have been left anywhere.
    appearance->setMode(ui::Appearance::ReverblabrDark);
    const QSignalSpy changed(appearance, &ui::Appearance::modeChanged);

    appearance->setMode(ui::Appearance::FollowSystem);
    EXPECT_EQ(file.written(), u"system"_s);
    EXPECT_EQ(ui::Appearance::storedMode(), ui::Appearance::FollowSystem);
    EXPECT_TRUE(appearance->followsSystem());

    appearance->setMode(ui::Appearance::ReverblabrLight);
    EXPECT_EQ(file.written(), u"reverblabr-light"_s);
    EXPECT_FALSE(appearance->followsSystem());
    EXPECT_FALSE(appearance->isDark());

    appearance->setMode(ui::Appearance::ReverblabrDark);
    EXPECT_EQ(file.written(), u"reverblabr-dark"_s);
    EXPECT_TRUE(appearance->isDark());
    EXPECT_EQ(changed.count(), 3);

    // Picking what is already picked changes nothing and says nothing.
    appearance->setMode(ui::Appearance::ReverblabrDark);
    EXPECT_EQ(changed.count(), 3);
}

TEST(AppearanceTest, BothFacesAreBundledInEveryWeightTheDesignUses)
{
    // The prototype loads Noto Sans at 400, 500, 600 and 700 and JetBrains
    // Mono at 500 and 600. **From the application, not the machine**: a
    // font database that found them only because they happen to be
    // installed here would pass on this machine and fail on the next.
    application();
    ui::Appearance::registerFonts();
    const QStringList families = QFontDatabase::families();
    ASSERT_TRUE(families.contains(u"Noto Sans"_s));
    ASSERT_TRUE(families.contains(u"JetBrains Mono"_s));

    for (const QFont::Weight weight : {QFont::Normal, QFont::Medium, QFont::DemiBold, QFont::Bold}) {
        QFont font(u"Noto Sans"_s);
        font.setWeight(weight);
        const QFontInfo info(font);
        EXPECT_EQ(info.family(), u"Noto Sans"_s) << weight;
        EXPECT_EQ(info.weight(), weight) << "Noto Sans at " << weight;
    }
    for (const QFont::Weight weight : {QFont::Medium, QFont::DemiBold}) {
        QFont font(u"JetBrains Mono"_s);
        font.setWeight(weight);
        const QFontInfo info(font);
        EXPECT_EQ(info.family(), u"JetBrains Mono"_s) << weight;
        EXPECT_EQ(info.weight(), weight) << "JetBrains Mono at " << weight;
        EXPECT_TRUE(info.fixedPitch());
    }
}

TEST(AppearanceTest, TheBundledFacesComeFromTheApplication)
{
    // Every file the application ships loads on its own, so a font the
    // resource list forgot is a failure here rather than a fallback face.
    application();
    for (const QString &file : {u"NotoSans-Regular.ttf"_s,
                                u"NotoSans-Medium.ttf"_s,
                                u"NotoSans-SemiBold.ttf"_s,
                                u"NotoSans-Bold.ttf"_s,
                                u"JetBrainsMono-Medium.ttf"_s,
                                u"JetBrainsMono-SemiBold.ttf"_s}) {
        const int id = QFontDatabase::addApplicationFont(u":/qt/qml/io/github/reverblabr/fonts/"_s + file);
        EXPECT_GE(id, 0) << file.toStdString();
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        EXPECT_TRUE(families.contains(file.startsWith(u"Noto"_s) ? u"Noto Sans"_s : u"JetBrains Mono"_s))
            << file.toStdString() << ": " << families.join(u", "_s).toStdString();
    }
}

TEST(AppearanceTest, APixelSizeBecomesThePointSizeOfTheSameHeight)
{
    // The prototype's scale has halves in it, which a QFont pixel size
    // cannot hold; as points they come out at the same height.
    application();
    const qreal dpi = qRound(QGuiApplication::primaryScreen()->logicalDotsPerInchY());
    EXPECT_DOUBLE_EQ(ui::Appearance::points(12.5) * dpi / 72.0, 12.5);
    EXPECT_DOUBLE_EQ(ui::Appearance::points(9.5) * dpi / 72.0, 9.5);

    // What is asked for, not what a raster engine rounds it to: the
    // scene graph draws text at fractional sizes.
    const QFont body = ui::Appearance::bodyFont();
    EXPECT_EQ(body.family(), u"Noto Sans"_s);
    EXPECT_DOUBLE_EQ(body.pointSizeF() * dpi / 72.0, 12.5);
}

TEST(AppearanceTest, TheDesignsFontIsTheApplicationsAndFollowingTheSystemGivesItBack)
{
    // **The application font is what every control not named in AppFonts
    // is set in**, so the Reverblabr appearance sets it and following the
    // system puts back what the desktop gave.
    application();
    SettingsFile file;
    ui::Appearance *appearance = ui::Appearance::instance();
    appearance->setMode(ui::Appearance::FollowSystem);
    const QFont desktops = QGuiApplication::font();

    appearance->setMode(ui::Appearance::ReverblabrLight);
    EXPECT_EQ(QGuiApplication::font().family(), u"Noto Sans"_s);
    EXPECT_DOUBLE_EQ(QGuiApplication::font().pointSizeF(), ui::Appearance::points(12.5));

    appearance->setMode(ui::Appearance::FollowSystem);
    EXPECT_EQ(QGuiApplication::font(), desktops);
}

TEST(AppearanceTest, AMenuRowWritesItsKeyAsTheDesktopDoes)
{
    // **A menu row puts its action's key at its far end**. An action
    // holds either words or one of the standard keys, which arrives as its
    // number; both come out as the platform writes them, and a standard key
    // with several bindings as its first.
    application();
    EXPECT_EQ(ui::Appearance::shortcutText(QVariant(u"Ctrl+O"_s)),
              QKeySequence(u"Ctrl+O"_s).toString(QKeySequence::NativeText));
    EXPECT_EQ(ui::Appearance::shortcutText(QVariant(u"Space"_s)),
              QKeySequence(Qt::Key_Space).toString(QKeySequence::NativeText));
    const QList<QKeySequence> zoomIn = QKeySequence::keyBindings(QKeySequence::ZoomIn);
    ASSERT_FALSE(zoomIn.isEmpty());
    EXPECT_EQ(ui::Appearance::shortcutText(QVariant(int(QKeySequence::ZoomIn))),
              zoomIn.constFirst().toString(QKeySequence::NativeText));
    EXPECT_FALSE(ui::Appearance::shortcutText(QVariant(int(QKeySequence::New))).isEmpty());
    // No key, no words.
    EXPECT_TRUE(ui::Appearance::shortcutText(QVariant()).isEmpty());
    EXPECT_TRUE(ui::Appearance::shortcutText(QVariant(QString())).isEmpty());
}
