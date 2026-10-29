// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "appinfo.h"

using namespace Qt::StringLiterals;

namespace rb::ui
{

AppInfo::AppInfo(QObject *parent)
    : QObject(parent)
{
}

KAboutData AppInfo::aboutData() const
{
    return KAboutData::applicationData();
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
