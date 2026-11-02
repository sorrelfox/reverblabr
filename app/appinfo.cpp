// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "appinfo.h"

#include "core/engineinfo.h"

#include <KCoreAddons>
#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

// **The project's addresses, in one place.** tools.desktop-entry holds them
// against the AppStream data. Documents go to `dev`, the default branch:
// `main` is empty until the first release, and `dev` outlives it.
constexpr auto kHomepage = "https://github.com/sorrelfox/reverblabr";
constexpr auto kBugTracker = "https://github.com/sorrelfox/reverblabr/issues";
constexpr auto kContribute = "https://github.com/sorrelfox/reverblabr/blob/dev/CONTRIBUTING.md";
// As every source file's header gives it.
constexpr auto kLicence = "https://mozilla.org/MPL/2.0/";

} // namespace

void describeApplication(KAboutData &about)
{
    about.setHomepage(QString::fromLatin1(kHomepage));
    // **Not KDE's tracker**, which is where KAboutData points by default
    // and is not where this project's bugs go. The same address the
    // translation template carries.
    about.setBugAddress(kBugTracker);
    about.addComponent(u"Qt"_s,
                       i18nc("@info", "Application framework"),
                       QString::fromLatin1(qVersion()),
                       u"https://www.qt.io"_s,
                       KAboutLicense::LGPL_V3);
    about.addComponent(u"KDE Frameworks"_s,
                       i18nc("@info", "Desktop libraries"),
                       KCoreAddons::versionString(),
                       u"https://develop.kde.org/products/frameworks/"_s);
    about.addComponent(u"PipeWire"_s,
                       i18nc("@info", "Audio and video server"),
                       rb::core::engineInfo().pipeWireLibrary,
                       u"https://pipewire.org"_s,
                       KAboutLicense::MIT);
}

QString licenceNameOf(const KAboutData &about)
{
    const QList<KAboutLicense> licences = about.licenses();
    return licences.isEmpty() ? QString() : licences.constFirst().name(KAboutLicense::FullName);
}

AppInfo::AppInfo(QObject *parent)
    : QObject(parent)
{
}

KAboutData AppInfo::aboutData() const
{
    return KAboutData::applicationData();
}

QUrl AppInfo::homepageUrl() const
{
    return QUrl(QString::fromLatin1(kHomepage));
}

QUrl AppInfo::contributeUrl() const
{
    return QUrl(QString::fromLatin1(kContribute));
}

QUrl AppInfo::licenceUrl() const
{
    return QUrl(QString::fromLatin1(kLicence));
}

QString AppInfo::licenceName() const
{
    return licenceNameOf(KAboutData::applicationData());
}

QUrl AppInfo::handbookUrl() const
{
    // The KDE scheme, so whatever the user has set to read documentation
    // opens it: KHelpCenter, as a rule. "reverblabr" is the folder
    // doc/CMakeLists.txt installs the handbook to, under
    // share/doc/HTML/<language>.
    return QUrl(u"help:/reverblabr"_s);
}

QUrl AppInfo::bugReportUrl() const
{
    const QString address = KAboutData::applicationData().bugAddress();
    if (address.isEmpty()) {
        return {};
    }
    // KAboutData holds either a URL or an address to write to.
    const QUrl url(address);
    return url.scheme().isEmpty() ? QUrl(u"mailto:"_s + address) : url;
}

} // namespace rb::ui
