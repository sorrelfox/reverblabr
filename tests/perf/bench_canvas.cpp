// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a frame of the session canvas costs: the real canvas, against the
// mock engine, with fifty tiles wired together and their meters moving.
//
// **The processor time a frame takes, not the frames a second.** Offscreen
// there is no compositor and no vertical blank, so how often frames arrive
// is a timer's business (tests/qml/tst_canvasframes.qml says the same, and
// skips there). What a frame costs this process to make is measurable
// anywhere: the bindings, the animations, the scene graph and the drawing,
// all of it on the processor when the software renderer draws. A canvas
// that got more expensive shows here whatever the screen is.
//
// The frame rate is written down too, for a run on a real display.

#include "appearance.h"
#include "appshell.h"
#include "core/mockengine.h"
#include "perfreport.h"
#include "recentapplications.h"
#include "sessions.h"
#include "windowstate.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>

#include <algorithm>
#include <ctime>
#include <iostream>
#include <vector>

Q_IMPORT_QML_PLUGIN(io_github_reverblabrPlugin)
Q_IMPORT_QML_PLUGIN(ReverblabrPlugin)

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] double processSeconds()
{
    timespec now{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

[[nodiscard]] QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software:
        return u"software"_s;
    case QSGRendererInterface::OpenGL:
        return u"opengl"_s;
    case QSGRendererInterface::Vulkan:
        return u"vulkan"_s;
    case QSGRendererInterface::Null:
        return u"null"_s;
    default:
        return u"other"_s;
    }
}

/// Counts a window's frames and what they cost, for the QML driver, which
/// sets each scene up and then asks for it to be measured.
class FrameMeter : public QObject
{
    Q_OBJECT

public:
    explicit FrameMeter(perf::Report &report, QObject *parent = nullptr)
        : QObject(parent)
        , m_report(report)
    {
    }

    /// Measures the next \a frames frames of \a window as \a name, and says
    /// measured() when it has.
    Q_INVOKABLE void measure(QQuickWindow *window, const QString &name, int frames, const QString &detail)
    {
        if (m_window == nullptr) {
            m_report.note("graphics", graphicsApiName(window->rendererInterface()->graphicsApi()).toStdString());
            m_report.note("platform", QGuiApplication::platformName().toStdString());
        }
        m_window = window;
        m_name = name;
        m_detail = detail;
        m_wanted = frames;
        m_intervals.clear();
        m_counted = -1;
        // **Counted when a frame is done**: on the software and the basic
        // render loops that is this thread, and the threaded loop sends it
        // here as a queued call, which is late but counts the same frames.
        m_connection = connect(window, &QQuickWindow::frameSwapped, this, &FrameMeter::frameDone);
        window->update();
    }

    Q_INVOKABLE void fail(const QString &why)
    {
        std::cerr << "canvas benchmark: " << why.toStdString() << "\n";
        QCoreApplication::exit(1);
    }

    Q_INVOKABLE void finish()
    {
        QCoreApplication::exit(0);
    }

Q_SIGNALS:
    void measured(const QString &name);

private:
    void frameDone()
    {
        // The first frame after the call starts the clock; the ones before
        // it belong to whatever the driver did to set the scene up.
        if (m_counted < 0) {
            m_cpuStarted = processSeconds();
            m_wall.start();
            m_last.start();
            m_counted = 0;
            return;
        }
        m_intervals.push_back(static_cast<double>(m_last.nsecsElapsed()) * 1e-6);
        m_last.restart();
        if (++m_counted < m_wanted) {
            return;
        }
        disconnect(m_connection);
        const double cpu = processSeconds() - m_cpuStarted;
        const double wall = static_cast<double>(m_wall.nsecsElapsed()) * 1e-9;
        std::ranges::sort(m_intervals);
        const double medianInterval = m_intervals[m_intervals.size() / 2];
        const double cpuPerFrame = cpu * 1000.0 / m_counted;
        const double fps = m_counted / wall;
        std::cout << m_name.toStdString() << ": " << cpuPerFrame << " ms of processor a frame, " << cpu / wall * 100.0
                  << " % of a core, " << fps << " frames a second (median interval " << medianInterval << " ms) over "
                  << m_counted << " frames\n";

        const std::string base = "canvas." + m_name.toStdString();
        m_report.add(perf::Metric{
            .name = base + ".cpu-per-frame",
            .value = cpuPerFrame,
            .unit = "ms",
            .better = perf::Better::Lower,
            // The scene graph's own work moves by more than a block's
            // does between two runs on one machine.
            .tolerance = 0.2,
            .noise = 0.1,
            .detail = m_detail.toStdString(),
        });
        // **And the share of a core the scene keeps busy**, which a
        // scene drawing more frames a second (a meter moving asks for
        // more) cannot hide behind a cheaper frame.
        m_report.add(perf::Metric{
            .name = base + ".cpu-share",
            .value = cpu / wall * 100.0,
            .unit = "% of a core",
            .better = perf::Better::Lower,
            .tolerance = 0.2,
            .noise = 1.0,
            .detail = m_detail.toStdString(),
        });
        m_report.add(perf::Metric{
            .name = base + ".fps",
            .value = fps,
            .unit = "frames a second",
            // A timer's figure offscreen, the display's on a real one:
            // kept, never judged.
            .better = perf::Better::Neither,
            .tolerance = std::nullopt,
            .noise = std::nullopt,
            .detail = "median interval " + std::to_string(medianInterval) + " ms",
        });
        Q_EMIT measured(m_name);
    }

    perf::Report &m_report;
    QQuickWindow *m_window = nullptr;
    QMetaObject::Connection m_connection;
    QString m_name;
    QString m_detail;
    int m_wanted = 0;
    int m_counted = -1;
    double m_cpuStarted = 0.0;
    QElapsedTimer m_wall;
    QElapsedTimer m_last;
    std::vector<double> m_intervals;
};

} // namespace

int main(int argc, char *argv[])
{
    // **Offscreen and drawn in software unless told otherwise**: the same
    // on a runner with no display as on a desktop, so two runs compare.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
    }
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("reverblabr");

    QCommandLineParser parser;
    const QCommandLineOption jsonOption(u"json"_s, u"Where to write the figures."_s, u"file"_s);
    const QCommandLineOption framesOption(
        u"frames"_s, u"How many frames each scene is measured over."_s, u"count"_s, u"300"_s);
    const QCommandLineOption tilesOption(u"tiles"_s, u"How many tiles the busy scene has."_s, u"count"_s, u"50"_s);
    parser.addOptions({jsonOption, framesOption, tilesOption});
    parser.addHelpOption();
    parser.process(app);

    ui::Appearance::useStyleFor(ui::Appearance::ReverblabrDark);

    // **State of its own**, as the QML tests have: nothing read from or
    // written to the person's own files.
    QTemporaryDir stateDirectory;
    const KSharedConfig::Ptr state =
        KSharedConfig::openConfig(QDir(stateDirectory.path()).filePath(u"state"_s), KConfig::SimpleConfig);
    ui::WindowState::setConfig(state);
    ui::RecentApplications::setConfig(state);
    ui::Sessions::setConfig(state);
    ui::Appearance::setConfig(state);

    // The mock: no PipeWire, and made-up levels for the blocks, which is
    // what lights the wires and moves the meters.
    core::MockEngine mock;
    ui::AppShell::setEngine(&mock, [&mock](const QString &sessionId) { return mock.telemetryOf(sessionId); });

    perf::Report report("canvas");
    FrameMeter meter(report);
    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.rootContext()->setContextProperty(u"Bench"_s, &meter);
    engine.rootContext()->setContextProperty(u"BenchEngine"_s, &mock);
    engine.rootContext()->setContextProperty(u"FrameCount"_s, parser.value(framesOption).toInt());
    engine.rootContext()->setContextProperty(u"TileCount"_s, parser.value(tilesOption).toInt());
    engine.load(QUrl::fromLocalFile(QStringLiteral(BENCH_CANVAS_QML)));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    const int code = QApplication::exec();
    if (code != 0) {
        return code;
    }
    const bool written = parser.isSet(jsonOption) ? report.write(parser.value(jsonOption).toStdString())
                                                  : report.writeToOutputFolder("canvas.json");
    if (!written) {
        std::cerr << "could not write the figures\n";
        return 1;
    }
    return 0;
}

#include "bench_canvas.moc"
