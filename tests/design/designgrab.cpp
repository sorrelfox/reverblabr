// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The application's side of the design comparison: the real window,
// against the mock engine, driven into each state the scene names and
// grabbed to a file. tests/design/design_compare.py runs it and compares
// what it wrote with the prototype.

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
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>

#include <memory>

Q_IMPORT_QML_PLUGIN(io_github_reverblabrPlugin)
Q_IMPORT_QML_PLUGIN(ReverblabrPlugin)

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

// What the QML driver cannot do for itself: take the whole window as it is
// showing, and write down what it measured.
class Grabber : public QObject
{
    Q_OBJECT

public:
    explicit Grabber(QString outDirectory, QObject *parent = nullptr)
        : QObject(parent)
        , m_out(std::move(outDirectory))
    {
    }

    /// The window as it is drawn, **the whole window rather than one item
    /// in it**, so that the menu bar, the header and whatever sits in the
    /// overlay are all in the picture. Answers whether it was written.
    Q_INVOKABLE bool grab(QQuickWindow *window, const QString &name)
    {
        const QImage image = window->grabWindow();
        return !image.isNull() && image.save(QDir(m_out).filePath(name + u".png"_s));
    }

    /// Writes \a json, the driver's `JSON.stringify()` of what it measured.
    Q_INVOKABLE bool writeManifest(const QString &json)
    {
        QFile file(QDir(m_out).filePath(u"manifest.json"_s));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(json.toUtf8());
        return true;
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
    // As the application starts: QApplication for the Breeze controls.
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("reverblabr");

    QCommandLineParser parser;
    const QCommandLineOption sceneOption(u"scene"_s, u"The scene and screenshots, as JSON."_s, u"file"_s);
    const QCommandLineOption outOption(u"out"_s, u"Where the screenshots and the manifest go."_s, u"directory"_s);
    const QCommandLineOption appearanceOption(u"appearance"_s, u"The appearance to grab in."_s, u"name"_s);
    parser.addOptions({sceneOption, outOption, appearanceOption});
    parser.addHelpOption();
    parser.process(app);

    // **The style the application would start in** for that appearance,
    // unless the environment says otherwise: the application picks
    // it before its window exists, so the grab does too.
    const bool own = parser.value(appearanceOption).startsWith(u"reverblabr-"_s);
    ui::Appearance::useStyleFor(own ? ui::Appearance::ReverblabrDark : ui::Appearance::FollowSystem);

    QFile sceneFile(parser.value(sceneOption));
    if (!sceneFile.open(QIODevice::ReadOnly) || !parser.isSet(outOption)) {
        qCritical("a scene file and an output directory are needed");
        return 2;
    }
    const QVariant scene = QJsonDocument::fromJson(sceneFile.readAll()).toVariant();

    // **State of its own**, as the QML tests have: which sessions were
    // open, the window's size and the remembered applications all go to a
    // file that is thrown away, never to the person's own.
    QTemporaryDir stateDirectory;
    const KSharedConfig::Ptr state =
        KSharedConfig::openConfig(QDir(stateDirectory.path()).filePath(u"state"_s), KConfig::SimpleConfig);
    ui::WindowState::setConfig(state);
    ui::RecentApplications::setConfig(state);
    ui::Sessions::setConfig(state);
    // The appearance too: DesignGrab.qml picks the one asked for,
    // and the choice is written to this file rather than the person's.
    ui::Appearance::setConfig(state);

    // The mock: no PipeWire, no sound, and a made-up level for a live block.
    core::MockEngine mock;
    mock.addFakeInventory();
    ui::AppShell::setEngine(&mock, [&mock](const QString &sessionId) { return mock.telemetryOf(sessionId); });

    Grabber grabber(parser.value(outOption));
    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.rootContext()->setContextProperty(u"Grabber"_s, &grabber);
    engine.rootContext()->setContextProperty(u"DesignEngine"_s, &mock);
    engine.rootContext()->setContextProperty(u"Scene"_s, scene);
    engine.rootContext()->setContextProperty(u"AppearanceName"_s, parser.value(appearanceOption));
    engine.load(QUrl::fromLocalFile(QStringLiteral(DESIGN_GRAB_QML)));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}

#include "designgrab.moc"
