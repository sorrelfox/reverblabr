// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Every unit test runs with its XDG directories under the build tree, so
// nothing a test saves lands in the home directory of whoever runs it.
// rb_add_gtest hands them over; for months only the first of the
// four arrived, and a store that asked Qt for its config directory was
// answered with the real ~/.config.

#include <QDir>
#include <QStandardPaths>
#include <gtest/gtest.h>

using namespace Qt::StringLiterals;

namespace
{

/// Where rb_add_gtest says the test's own home is.
QString testHome()
{
    return QDir::cleanPath(QString::fromUtf8(RB_TEST_HOME));
}

/// True when `path` is `directory` or somewhere inside it.
bool isInside(const QString &path, const QString &directory)
{
    const QString cleaned = QDir::cleanPath(path);
    return cleaned == directory || cleaned.startsWith(directory + u'/');
}

} // namespace

TEST(TestHome, EveryXdgVariableArrivesAndIsTheTestsOwn)
{
    for (const char *variable : {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) {
        if (!qEnvironmentVariableIsSet(variable)) {
            ADD_FAILURE() << variable << " never reached the test";
            continue;
        }
        const QString value = qEnvironmentVariable(variable);
        EXPECT_TRUE(isInside(value, testHome())) << variable << " is " << value.toStdString();
    }
}

TEST(TestHome, WhereQtWouldSaveAnythingIsInsideIt)
{
    for (const auto location : {QStandardPaths::GenericDataLocation,
                                QStandardPaths::GenericConfigLocation,
                                QStandardPaths::GenericStateLocation,
                                QStandardPaths::GenericCacheLocation,
                                QStandardPaths::AppDataLocation,
                                QStandardPaths::AppConfigLocation,
                                QStandardPaths::AppLocalDataLocation,
                                QStandardPaths::StateLocation,
                                QStandardPaths::CacheLocation}) {
        const QString path = QStandardPaths::writableLocation(location);
        EXPECT_TRUE(isInside(path, testHome()))
            << "location " << static_cast<int>(location) << " is " << path.toStdString();
    }
}
