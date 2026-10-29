// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The key screens in the pseudo-locale: every word the catalog knows
// comes out as ⟦áççéñtéð ẋǿẋǿ⟧, about 40 % longer. PseudoCheck.qml opens each
// screen, walks what is drawn and reports two things: text without the
// brackets, which never went through the catalog, and text the longer words
// no longer fit. The screenshots go beside the report, for a person to look
// at. Offscreen, on the mock engine, with state of its own.

#include "appearance.h"
#include "appshell.h"
#include "core/mockengine.h"
#include "recentapplications.h"
#include "sessions.h"
#include "windowstate.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(io_github_reverblabrPlugin)
Q_IMPORT_QML_PLUGIN(ReverblabrPlugin)

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

class Checker : public QObject
{
    Q_OBJECT

public:
    explicit Checker(QString outDirectory, QObject *parent = nullptr)
        : QObject(parent)
        , m_out(std::move(outDirectory))
    {
    }

    /// The whole window as drawn, for a person to look at.
    Q_INVOKABLE bool grab(QQuickWindow *window, const QString &name)
    {
        const QImage image = window->grabWindow();
        return !image.isNull() && image.save(QDir(m_out).filePath(name + u".png"_s));
    }

    /// Writes the findings, the driver's `JSON.stringify()` of them.
    Q_INVOKABLE bool writeReport(const QString &json)
    {
        QFile file(QDir(m_out).filePath(u"report.json"_s));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(json.toUtf8());
        return true;
    }

    /// The text of the ToolTip attached to \a item, if it has one: how text
    /// that elides by design is still read in full. An attached object is
    /// created with its item as QObject parent, so it is found among the
    /// item's children without Qt Quick's private headers.
    Q_INVOKABLE QString toolTipOf(QObject *item) const
    {
        if (item == nullptr) {
            return {};
        }
        for (const QObject *child : item->children()) {
            if (qstrcmp(child->metaObject()->className(), "QQuickToolTipAttached") == 0) {
                return child->property("text").toString();
            }
        }
        return {};
    }

    Q_INVOKABLE void finish(int code)
    {
        QCoreApplication::exit(code);
    }

private:
    QString m_out;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    const QCommandLineOption localeOption(u"locale-dir"_s, u"Where qps/LC_MESSAGES/reverblabr.mo is."_s, u"dir"_s);
    const QCommandLineOption outOption(u"out"_s, u"Where the screenshots and report.json go."_s, u"directory"_s);
    const QCommandLineOption appearanceOption(u"appearance"_s, u"reverblabr-dark or system-dark."_s, u"name"_s);
    const QCommandLineOption injectOption(
        u"inject"_s, u"Put one known fault on the first screen: hardcoded or truncated."_s, u"fault"_s);
    parser.addOptions({localeOption, outOption, appearanceOption, injectOption});
    parser.addHelpOption();
    parser.process(app);
    if (!parser.isSet(localeOption) || !parser.isSet(outOption)) {
        qCritical("--locale-dir and --out are needed");
        return 2;
    }

    // The pseudo-locale, and nothing else: a word it doesn't have stays in
    // English, which is exactly what the check looks for.
    KLocalizedString::setApplicationDomain("reverblabr");
    KLocalizedString::addDomainLocaleDir("reverblabr", parser.value(localeOption));
    KLocalizedString::setLanguages({u"qps"_s});
    if (!KLocalizedString::isApplicationTranslatedInto(u"qps"_s)) {
        qCritical("the pseudo-locale catalog did not load; gettext needs a real locale such as en_US.UTF-8");
        return 3;
    }

    const bool own = parser.value(appearanceOption).startsWith(u"reverblabr-"_s);
    ui::Appearance::useStyleFor(own ? ui::Appearance::ReverblabrDark : ui::Appearance::FollowSystem);

    // State of its own, thrown away, as the QML tests and the design grab have.
    QTemporaryDir stateDirectory;
    const KSharedConfig::Ptr state =
        KSharedConfig::openConfig(QDir(stateDirectory.path()).filePath(u"state"_s), KConfig::SimpleConfig);
    ui::WindowState::setConfig(state);
    ui::RecentApplications::setConfig(state);
    ui::Sessions::setConfig(state);
    ui::Appearance::setConfig(state);

    core::MockEngine mock;
    mock.addFakeInventory();
    mock.addFakeVirtualDevices();
    ui::AppShell::setEngine(&mock, [&mock](const QString &sessionId) { return mock.telemetryOf(sessionId); });

    Checker checker(parser.value(outOption));
    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.rootContext()->setContextProperty(u"Checker"_s, &checker);
    engine.rootContext()->setContextProperty(u"CheckEngine"_s, &mock);
    engine.rootContext()->setContextProperty(u"AppearanceName"_s, parser.value(appearanceOption));
    engine.rootContext()->setContextProperty(u"Inject"_s, parser.value(injectOption));
    engine.load(QUrl::fromLocalFile(QStringLiteral(PSEUDO_CHECK_QML)));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}

#include "pseudocheck.moc"
