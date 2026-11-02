// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the application says of itself: where the About dialog's links go,
// and what it lists as built on. tools.desktop-entry holds the addresses
// against the AppStream data.

#include "appinfo.h"

#include <gtest/gtest.h>

using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] KAboutData describedApplication()
{
    KAboutData about(u"reverblabr"_s, u"Reverblabr"_s, u"1.2.3"_s, u"What it does"_s, KAboutLicense::MPL_V2);
    describeApplication(about);
    return about;
}

} // namespace

TEST(AppInfoTest, TheRecordSaysWhereTheProjectIs)
{
    const KAboutData about = describedApplication();
    const AppInfo info;

    EXPECT_EQ(about.homepage(), u"https://github.com/sorrelfox/reverblabr"_s);
    EXPECT_EQ(info.homepageUrl().toString(), about.homepage());
    EXPECT_EQ(about.bugAddress(), u"https://github.com/sorrelfox/reverblabr/issues"_s);
}

TEST(AppInfoTest, TheLinksGoToPagesThatAreThere)
{
    const AppInfo info;

    // Not main, which is empty until the first release.
    EXPECT_EQ(info.contributeUrl().toString(), u"https://github.com/sorrelfox/reverblabr/blob/dev/CONTRIBUTING.md"_s);
    EXPECT_EQ(info.licenceUrl().toString(), u"https://mozilla.org/MPL/2.0/"_s);
}

TEST(AppInfoTest, TheLicenceIsNamedInFull)
{
    // The record's own `name` is the short form, which says little to anybody.
    EXPECT_EQ(licenceNameOf(describedApplication()), u"Mozilla Public License 2.0"_s);
}

TEST(AppInfoTest, EveryLibraryHasItsVersionAndItsAddress)
{
    const QList<KAboutComponent> components = describedApplication().components();

    QStringList names;
    for (const KAboutComponent &component : components) {
        names.append(component.name());
        EXPECT_TRUE(component.webAddress().startsWith(u"https://"_s)) << component.name().toStdString();
    }
    for (const QString &library : {u"Qt"_s, u"KDE Frameworks"_s, u"PipeWire"_s}) {
        EXPECT_TRUE(names.contains(library)) << library.toStdString();
    }

    const auto versionOf = [&components](const QString &name) {
        for (const KAboutComponent &component : components) {
            if (component.name() == name) {
                return component.version();
            }
        }
        return QString();
    };
    EXPECT_EQ(versionOf(u"Qt"_s), QString::fromLatin1(qVersion()));
    EXPECT_FALSE(versionOf(u"KDE Frameworks"_s).isEmpty());
}
