// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/errorcodes.h"
#include "core/virtualdevices.h"
#include "errormessages.h"

#include <QRegularExpression>
#include <gtest/gtest.h>

#include <string>

namespace
{

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string join(const QStringList &items)
{
    return items.join(QStringLiteral(", ")).toStdString();
}

} // namespace

// The engine may only report codes the GUI can translate.
TEST(ErrorMessages, EveryEngineCodeHasAMessage)
{
    QStringList missing;
    for (const rb::core::ErrorCodeInfo &info : rb::core::kErrorCodes) {
        if (!rb::app::hasErrorMessage(toQString(info.name))) {
            missing.append(toQString(info.name));
        }
    }
    EXPECT_TRUE(missing.isEmpty()) << "no GUI message for: " << join(missing);
}

// A message for a code the engine no longer declares is dead text for translators.
TEST(ErrorMessages, EveryMessageBelongsToAnEngineCode)
{
    QStringList engineCodes;
    for (const rb::core::ErrorCodeInfo &info : rb::core::kErrorCodes) {
        engineCodes.append(toQString(info.name));
    }
    QStringList orphans;
    for (const QString &code : rb::app::errorMessageCodes()) {
        if (!engineCodes.contains(code)) {
            orphans.append(code);
        }
    }
    EXPECT_TRUE(orphans.isEmpty()) << "messages without an engine code: " << join(orphans);
}

// Each message uses exactly the arguments the engine declares: every value
// appears, and KI18n reports no missing or surplus placeholders.
TEST(ErrorMessages, ArgumentsMatchThePlaceholders)
{
    for (const rb::core::ErrorCodeInfo &info : rb::core::kErrorCodes) {
        QVariantMap args;
        for (const std::string_view name : info.arguments) {
            if (!name.empty()) {
                args.insert(toQString(name), QStringLiteral("<value of %1>").arg(toQString(name)));
            }
        }
        const QString message = rb::app::errorMessage(toQString(info.name), args);
        SCOPED_TRACE(std::string(info.name));
        EXPECT_FALSE(message.isEmpty());
        EXPECT_FALSE(message.contains(QStringLiteral("I18N_"))) << message.toStdString();
        EXPECT_FALSE(message.contains(QRegularExpression(QStringLiteral("%\\d")))) << message.toStdString();
        for (auto it = args.cbegin(); it != args.cend(); ++it) {
            EXPECT_TRUE(message.contains(it.value().toString())) << message.toStdString();
        }
    }
}

// A reason is the engine's own vocabulary — the name of an enumerator — and
// has to become words on the way into the sentence that carries it.
TEST(ErrorMessages, AReasonBecomesWords)
{
    const QString reason = QStringLiteral("NodeRefused");
    const QString words = rb::app::reasonMessage(reason);
    EXPECT_NE(words, reason);
    EXPECT_FALSE(words.isEmpty());

    QVariantMap args;
    args.insert(QStringLiteral("session"), QStringLiteral("Podcast"));
    args.insert(QStringLiteral("reason"), reason);
    const QString message = rb::app::errorMessage(QStringLiteral("session.cannotRun"), args);
    EXPECT_TRUE(message.contains(QStringLiteral("Podcast"))) << message.toStdString();
    EXPECT_TRUE(message.contains(words)) << message.toStdString();
    EXPECT_FALSE(message.contains(reason)) << message.toStdString();
}

// Half a sentence a reader can act on beats a sentence that says nothing, so
// a reason the table does not know is shown as it came.
TEST(ErrorMessages, AnUnknownReasonIsPassedThroughRatherThanSwallowed)
{
    const QString reason = QStringLiteral("SomethingThisBuildHasNotLearned");
    EXPECT_EQ(rb::app::reasonMessage(reason), reason);

    QVariantMap args;
    args.insert(QStringLiteral("reason"), reason);
    EXPECT_TRUE(rb::app::errorMessage(QStringLiteral("edit.refused"), args).contains(reason));
}

TEST(ErrorMessages, UnknownCodeGetsAGenericMessageNamingIt)
{
    const QString message = rb::app::errorMessage(QStringLiteral("test.notARealCode"));
    EXPECT_TRUE(message.contains(QStringLiteral("test.notARealCode"))) << message.toStdString();
    EXPECT_FALSE(rb::app::hasErrorMessage(QStringLiteral("test.notARealCode")));
}

// **Every reason a virtual device can be refused with has words**.
// The engine counts its reasons up from its own errors, so an error added
// there without a phrase here fails this.
TEST(ErrorMessages, EveryVirtualDeviceReasonHasWords)
{
    for (const QString &reason : rb::core::virtualDeviceReasons()) {
        SCOPED_TRACE(reason.toStdString());
        const QString words = rb::app::reasonMessage(reason);
        EXPECT_FALSE(words.isEmpty());
        EXPECT_NE(words, reason);
    }
}

// **A format this system cannot write says how to get it**, both
// where a panel says why it is not offered and where a recording that asked
// for it stops.
TEST(ErrorMessages, AMissingEncoderPointsAtTheFullFfmpegAndTheFormatsThatWork)
{
    for (const char *reason : {"encoderMissing", "EncoderMissing"}) {
        const QString words = rb::app::reasonMessage(QString::fromLatin1(reason));
        EXPECT_TRUE(words.contains(QStringLiteral("RPM Fusion"))) << reason;
        EXPECT_TRUE(words.contains(QStringLiteral("Opus")) && words.contains(QStringLiteral("FLAC"))) << reason;
    }
    QVariantMap args;
    args.insert(QStringLiteral("file"), QStringLiteral("/music/take.m4a"));
    args.insert(QStringLiteral("reason"), QStringLiteral("EncoderMissing"));
    EXPECT_TRUE(rb::app::errorMessage(QStringLiteral("recording.failed"), args).contains(QStringLiteral("RPM Fusion")));
}
