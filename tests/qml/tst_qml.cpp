// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "../unit/dsp/earblock.h"
#include "appearance.h"
#include "appshell.h"
#include "core/mockengine.h"
#include "diagnostics.h"
#include "fileactions.h"
#include "recentapplications.h"
#include "sessions.h"
#include "systemdevicewriter.h"
#include "windowstate.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuickTest>

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

Q_IMPORT_QML_PLUGIN(io_github_reverblabrPlugin)
Q_IMPORT_QML_PLUGIN(ReverblabrPlugin)

// Prepares each test's QML engine the same way the application does.
using namespace rb;
using namespace Qt::StringLiterals;

/// **The all-users helper, as the QML tests have it**: no system
/// bus, no polkit and no password — a stand-in a test tells how to answer.
///
/// By default it answers, after the call, that no helper is installed, so a
/// test that never thinks about it can never reach the desktop. A test sets
/// `answer` to a reason, or to "" for a helper that was let through and
/// wrote the list (which the mock then holds); with `hold` set, the request
/// waits for `release()`, which is how a test looks at the window while the
/// password is being asked for.
class FakeHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString answer MEMBER m_answer NOTIFY changed)
    Q_PROPERTY(bool hold MEMBER m_hold NOTIFY changed)
    Q_PROPERTY(int requests READ requests NOTIFY changed)
    Q_PROPERTY(bool waiting READ waiting NOTIFY changed)
    /// The list the last request asked to be written.
    Q_PROPERTY(QVariantList lastDevices READ lastDevices NOTIFY changed)

public:
    explicit FakeHelper(core::MockEngine *engine)
        : m_engine(engine)
    {
    }

    void run(const QVariantMap &arguments, const ui::SystemDeviceWriter::Done &done)
    {
        ++m_requests;
        m_lastDevices = arguments.value(u"devices"_s).toList();
        m_done = done;
        Q_EMIT changed();
        if (!m_hold) {
            QMetaObject::invokeMethod(this, &FakeHelper::release, Qt::QueuedConnection);
        }
    }

    [[nodiscard]] int requests() const
    {
        return m_requests;
    }
    [[nodiscard]] bool waiting() const
    {
        return static_cast<bool>(m_done);
    }
    [[nodiscard]] QVariantList lastDevices() const
    {
        return m_lastDevices;
    }

    /// Back to a helper that is not installed.
    Q_INVOKABLE void reset()
    {
        m_answer = QString(ui::SystemDeviceWriter::kUnavailable);
        m_hold = false;
        m_requests = 0;
        m_lastDevices.clear();
        m_done = nullptr;
        Q_EMIT changed();
    }

    /// Answers the request that is out with `answer`.
    Q_INVOKABLE void release()
    {
        if (!m_done) {
            return;
        }
        const ui::SystemDeviceWriter::Done done = std::exchange(m_done, nullptr);
        if (m_answer.isEmpty()) {
            m_engine->writeFakeSystemVirtualDevices(m_lastDevices);
        }
        Q_EMIT changed();
        done(m_answer);
    }

Q_SIGNALS:
    void changed();

private:
    core::MockEngine *m_engine;
    QString m_answer = QString(ui::SystemDeviceWriter::kUnavailable);
    bool m_hold = false;
    int m_requests = 0;
    QVariantList m_lastDevices;
    ui::SystemDeviceWriter::Done m_done;
};

/// **The desktop's file actions, answered here**. No test opens a
/// file manager, starts an application or touches anybody's trash: this is
/// told which action on which file, and a trash moves the file into a
/// folder beside the mock's recordings, which is what makes it leave the
/// list on the next rescan.
class FakeFileActions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString lastAction READ lastAction NOTIFY changed)
    Q_PROPERTY(QString lastPath READ lastPath NOTIFY changed)

public:
    explicit FakeFileActions(core::MockEngine *engine)
        : m_engine(engine)
    {
    }

    [[nodiscard]] int count() const
    {
        return m_count;
    }
    [[nodiscard]] QString lastAction() const
    {
        return m_lastAction;
    }
    [[nodiscard]] QString lastPath() const
    {
        return m_lastPath;
    }

    /// Removes \a path the way a file manager would, telling nobody.
    Q_INVOKABLE bool removeBehindTheListsBack(const QString &path)
    {
        return QFile::remove(path);
    }

    /// The next action fails with \a error, once.
    Q_INVOKABLE void failNext(const QString &error)
    {
        m_failNext = error;
    }

    void run(ui::FileActions::Action action, const QUrl &url, const ui::FileActions::Done &done)
    {
        ++m_count;
        m_lastPath = url.toLocalFile();
        switch (action) {
        case ui::FileActions::Action::ShowInFolder:
            m_lastAction = u"showInFolder"_s;
            break;
        case ui::FileActions::Action::OpenWith:
            m_lastAction = u"openWith"_s;
            break;
        case ui::FileActions::Action::MoveToTrash:
            m_lastAction = u"moveToTrash"_s;
            break;
        case ui::FileActions::Action::Open:
            m_lastAction = u"open"_s;
            break;
        }
        Q_EMIT changed();
        if (!m_failNext.isEmpty()) {
            done(std::exchange(m_failNext, QString{}));
            return;
        }
        if (action == ui::FileActions::Action::MoveToTrash) {
            const QString trash = QDir(m_engine->fakeRecordingFolder()).filePath(u"../trash"_s);
            QDir().mkpath(trash);
            if (!QFile::rename(m_lastPath, QDir(trash).filePath(QFileInfo(m_lastPath).fileName()))) {
                done(u"The file could not be moved"_s);
                return;
            }
        }
        done(QString{});
    }

Q_SIGNALS:
    void changed();

private:
    core::MockEngine *m_engine;
    int m_count = 0;
    QString m_lastAction;
    QString m_lastPath;
    QString m_failNext;
};

/// **Where the diagnostics bundle goes in a test**: never the
/// clipboard of whatever desktop the suite happens to run on.
class FakeClipboard : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)

public:
    [[nodiscard]] QString text() const
    {
        return m_text;
    }

    void set(const QString &text)
    {
        m_text = text;
        Q_EMIT changed();
    }

    Q_INVOKABLE void clear()
    {
        set(QString());
    }

Q_SIGNALS:
    void changed();

private:
    QString m_text;
};

/// **What a screen reader is told**: every item's Accessible
/// properties — its role, its name, whether it is left out — walked from an
/// item down, with accessibility switched on, which is when the controls fill
/// in their own roles and names (a button its text) the way they do for Orca.
///
/// **Read from the items, not asked of Qt's accessibility interfaces.** An
/// interface is made for each item it is asked about and kept until the item
/// goes, and a window torn down with a few thousand of them brought the QML
/// engine's garbage collector down (Qt 6.11, every run with every panel
/// walked; none with the walk left out, even under QV4_MM_AGGRESSIVE_GC).
/// The Accessible properties are what those interfaces read their role and
/// name from, so reading them directly checks the same thing.
class AccessibilityAudit : public QObject
{
    Q_OBJECT

public:
    /// The roles a person can act on: the ones that have to say what they
    /// are for. A picture, a label or a pane says itself or nothing.
    static bool acts(QAccessible::Role role)
    {
        switch (role) {
        case QAccessible::Button:
        case QAccessible::CheckBox:
        case QAccessible::RadioButton:
        case QAccessible::ComboBox:
        case QAccessible::SpinBox:
        case QAccessible::Slider:
        case QAccessible::Dial:
        case QAccessible::EditableText:
        case QAccessible::PageTab:
        case QAccessible::Link:
        case QAccessible::ButtonMenu:
        case QAccessible::ButtonDropDown:
        case QAccessible::MenuItem:
            return true;
        default:
            return false;
        }
    }

    /// An item's Accessible attached object, or null. **Never made here**:
    /// asking for one would put the item into the accessibility tree, which
    /// is exactly what an item without Accessible properties is not in. It
    /// is the item's child when it exists.
    static QObject *accessibleOf(const QObject *item)
    {
        for (QObject *child : item->children()) {
            if (child->inherits("QQuickAccessibleAttached")) {
                return child;
            }
        }
        return nullptr;
    }

    /// Where an item is, as the chain of types and names above it, so a
    /// finding says which control it is.
    static QString pathOf(const QObject *item)
    {
        QStringList parts;
        for (const QObject *object = item; object != nullptr && parts.size() < 5; object = object->parent()) {
            QString part = QString::fromLatin1(object->metaObject()->className()).section(u"_QML"_s, 0, 0);
            if (!object->objectName().isEmpty()) {
                part += u"#"_s + object->objectName();
            }
            parts.prepend(part);
        }
        return parts.join(u" > "_s);
    }

    /// Switches accessibility on or off for the whole process, as a screen
    /// reader starting or stopping does. The offscreen platform has no bridge
    /// to the desktop's accessibility bus, so nothing leaves the test.
    Q_INVOKABLE void setActive(bool active) const
    {
        QAccessible::setActive(active);
    }

    /// Every item under \a root that a person can act on and that has no
    /// name, as `{role, path}`. What is not shown is not looked at.
    Q_INVOKABLE QVariantList unnamed(QQuickItem *root) const
    {
        QVariantList found;
        int looked = 0;
        walk(root, found, looked, 0);
        return found;
    }

    /// How many items under \a root are in the accessibility tree, for a test
    /// that has to know the walk looked at something.
    Q_INVOKABLE int census(QQuickItem *root) const
    {
        QVariantList found;
        int looked = 0;
        walk(root, found, looked, 0);
        return looked;
    }

    /// What one item says about itself: `{role, name, description}`.
    Q_INVOKABLE QVariantMap describe(QQuickItem *item) const
    {
        QObject *attached = item == nullptr ? nullptr : accessibleOf(item);
        if (attached == nullptr) {
            return {};
        }
        return {{u"role"_s, attached->property("role").toInt()},
                {u"name"_s, attached->property("name").toString()},
                {u"description"_s, attached->property("description").toString()}};
    }

private:
    /// **What Basic does and the desktop style does not.** The QML suite
    /// runs the desktop's style as Basic, which has no display to draw it
    /// with; Basic leaves a non-editable combo box's text field in the tree
    /// and gives a spin box's field no name, where the desktop style hides
    /// the first and names the second after its box (as the Reverblabr style
    /// now does too). Those two, under Basic only, are Basic's and not the
    /// window's — the Reverblabr-style suite still holds its own controls
    /// to it.
    static bool standInsOwnEditor(const QQuickItem *item, QAccessible::Role role)
    {
        if (qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE") != u"Basic"_s || role != QAccessible::EditableText) {
            return false;
        }
        const QObject *box = item->parent();
        if (box == nullptr) {
            return false;
        }
        if (box->inherits("QQuickSpinBox")) {
            return true;
        }
        return box->inherits("QQuickComboBox") && !box->property("editable").toBool();
    }

    /// **Words that can be selected but not changed** — a message's text in
    /// a read-only field, as Kirigami's InlineMessage draws it. What such a
    /// field is read out as is what it says, and that is its name's job done.
    static bool readsItself(const QQuickItem *item, QAccessible::Role role)
    {
        return role == QAccessible::EditableText && item->property("readOnly").toBool()
            && !item->property("text").toString().trimmed().isEmpty();
    }

    static void walk(QQuickItem *item, QVariantList &found, int &looked, int depth)
    {
        if (item == nullptr || !item->isVisible() || depth > 400) {
            return;
        }
        if (QObject *attached = accessibleOf(item); attached != nullptr && !attached->property("ignored").toBool()) {
            ++looked;
            const auto role = QAccessible::Role(attached->property("role").toInt());
            if (acts(role) && attached->property("name").toString().trimmed().isEmpty()
                && !standInsOwnEditor(item, role) && !readsItself(item, role)) {
                found.append(QVariantMap{{u"role"_s, int(role)}, {u"path"_s, pathOf(item)}});
            }
        }
        for (QQuickItem *child : item->childItems()) {
            walk(child, found, looked, depth + 1);
        }
    }
};

/// **An error in the QML fails the run**. A binding that throws, a
/// function called on an object that is already gone, a binding loop: the
/// engine says so on the console and goes on, the test that happened to be
/// running passes, and the warning scrolls past in a log nobody reads — a
/// TypeError in the Parametric EQ's panel did exactly that through three
/// suites. Every warning of that kind is noted once, with the test it came
/// during, the run goes on so that all of them are seen at once, and the
/// run fails at the end, listing them.
///
/// **Two ways in, because neither sees everything:**
/// - each engine's own `warnings` signal, which is there from the first
///   file on, whoever is printing; but not every warning goes through it (a
///   layout's binding loop on a delegate did not);
/// - a message handler in front of the harness's, for those. The harness
///   installs its own when the logging starts, after the first file's
///   engine is made, and passes nothing on, so the handler is put back in
///   front of it with every file's engine: from the second file on, it sees
///   everything the log does.
///
/// Nothing is allowed through: no test expects one of these (a test that
/// does would say so with `ignoreWarning`, and `tst_settingspanel` asks
/// for a binding loop to fail its own test with `failOnWarning`).
class QmlErrorWatch
{
public:
    /// Watches one test file's engine, and puts the handler back in front.
    static void watch(QQmlEngine *engine)
    {
        QObject::connect(engine, &QQmlEngine::warnings, engine, [](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                note(warning.toString());
            }
        });
        const QtMessageHandler current = qInstallMessageHandler(&QmlErrorWatch::handle);
        if (current != &QmlErrorWatch::handle) {
            s_previous = current;
        }
    }

    /// Takes the handler out, prints what was noted, and says how many there
    /// were. **Out before anything is torn down**: a message from a
    /// destructor after main would otherwise reach a handler whose list and
    /// mutex are already gone, and crash the run.
    static int report()
    {
        qInstallMessageHandler(nullptr);
        const std::lock_guard lock(s_mutex);
        for (const QString &line : std::as_const(s_noted)) {
            std::fprintf(stderr, "FAIL!  : QML error during the tests: %s\n", qPrintable(line));
        }
        if (!s_noted.isEmpty()) {
            std::fprintf(stderr,
                         "%lld QML error(s) during the tests; see the lines above.\n",
                         static_cast<long long>(s_noted.size()));
        }
        return static_cast<int>(s_noted.size());
    }

private:
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (type != QtDebugMsg && type != QtInfoMsg) {
            note(message);
        }
        if (s_previous != nullptr) {
            s_previous(type, context, message);
        }
    }

    /// Notes a warning of the error kind, once: the same one can come both
    /// ways.
    static void note(const QString &message)
    {
        static const QRegularExpression errors(
            uR"(\b(TypeError|ReferenceError|RangeError|SyntaxError|URIError|EvalError):)"
            uR"(|Binding loop detected)"
            uR"(|Unable to assign )"
            uR"(|Cannot (read|call|assign))"_s);
        if (!errors.match(message).hasMatch()) {
            return;
        }
        const char *test = QTest::currentTestFunction();
        const QString line = u"%1: %2"_s.arg(QString::fromLatin1(test != nullptr ? test : "(between tests)"), message);
        const std::lock_guard lock(s_mutex);
        if (!s_noted.contains(line)) {
            s_noted.append(line);
        }
    }

    static inline QtMessageHandler s_previous = nullptr;
    static inline std::mutex s_mutex;
    static inline QStringList s_noted;
};

class Setup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void applicationAvailable()
    {
        KLocalizedString::setApplicationDomain("reverblabr");
        // **A state file of the test's own.** The window writes down its
        // size and which of the library's categories are folded away, and a
        // test that ran against the real one would reach into the home
        // directory of whoever is running it — and be told by the last run
        // what to expect.
        m_state = KSharedConfig::openConfig(m_stateDirectory.filePath(u"state"_s), KConfig::SimpleConfig);
        ui::WindowState::setConfig(m_state);
        // The applications the pickers remember go in the same file, and
        // for the same reason: a test that wrote to the real one would be
        // offering whoever ran it their own programs back.
        ui::RecentApplications::setConfig(m_state);
        // And which sessions were open, for the same reason again: a
        // test that read the real one would be handed whatever the person
        // running it last had open.
        ui::Sessions::setConfig(m_state);
        // And the appearance.
        ui::Appearance::setConfig(m_state);
        // **Each run in the appearance its style goes with**, as the
        // application starts: its own style for a Reverblabr appearance —
        // the default, dark — and the desktop's, for which Basic stands in
        // here, for *Follow system*. A test that picks another puts this one
        // back (`SuiteAppearance`).
        m_suiteAppearance = ui::Appearance::ownStyle() ? ui::Appearance::ReverblabrDark : ui::Appearance::FollowSystem;
        ui::Appearance::instance()->setMode(m_suiteAppearance);
        // **The mock, exactly as the application hands over the real one.**
        // The QML cannot tell which it has, which is what makes a test of
        // the window a test of the window and not of PipeWire.
        m_engine = std::make_unique<core::MockEngine>();
        // **A block for one ear**, so that what every such block
        // shows — the L or R on its tile — is tested before any hearing
        // block ships. Registered here, not from QML, as the engine's own
        // blocks are.
        m_engine->addBlockTypeForTesting(std::string{test::OneEarBlock::kTypeId}, &test::OneEarBlock::make);
        // **Telemetry is handed over separately**, as it is for the real
        // engine ([ADR-0003]) — and it is what lets a test give a block a
        // signal, so that a live wire and a lit state dot are things the
        // window can be asked about.
        core::MockEngine *mock = m_engine.get();
        ui::AppShell::setEngine(mock, [mock](const QString &sessionId) { return mock->telemetryOf(sessionId); });
        // **Never the system bus.** A change to an all-users device goes to
        // the helper through KAuth, which would ask the desktop for an
        // administrator's password; here a stand-in answers instead.
        m_helper = std::make_unique<FakeHelper>(mock);
        m_files = std::make_unique<FakeFileActions>(mock);
        FakeFileActions *files = m_files.get();
        ui::FileActions::setRunnerForTesting(
            [files](ui::FileActions::Action action, const QUrl &url, QWindow *, const ui::FileActions::Done &done) {
                files->run(action, url, done);
            });
        FakeClipboard *clipboard = &m_clipboard;
        ui::Diagnostics::setClipboardSinkForTesting([clipboard](const QString &text) { clipboard->set(text); });
        FakeHelper *helper = m_helper.get();
        ui::SystemDeviceWriter::setRunnerForTesting(
            [helper](const QVariantMap &arguments, QWindow *, ui::SystemDeviceWriter::Done done) {
                helper->run(arguments, done);
            });
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        QmlErrorWatch::watch(engine);
        KLocalization::setupLocalizedContext(engine);
        // **The mock itself, for a test that needs a session to look at.**
        // Its calls are slots and its answers are signals, so QML can drive
        // it exactly as the window does — which is the point of the facade.
        engine->rootContext()->setContextProperty(u"TestEngine"_s, m_engine.get());
        engine->rootContext()->setContextProperty(u"SuiteAppearance"_s, int(m_suiteAppearance));
        engine->rootContext()->setContextProperty(u"TestHelper"_s, m_helper.get());
        engine->rootContext()->setContextProperty(u"TestFiles"_s, m_files.get());
        engine->rootContext()->setContextProperty(u"TestClipboard"_s, &m_clipboard);
        engine->rootContext()->setContextProperty(u"TestAccessibility"_s, &m_accessibility);
    }

private:
    ui::Appearance::Mode m_suiteAppearance = ui::Appearance::ReverblabrDark;
    std::unique_ptr<core::MockEngine> m_engine;
    std::unique_ptr<FakeHelper> m_helper;
    std::unique_ptr<FakeFileActions> m_files;
    FakeClipboard m_clipboard;
    AccessibilityAudit m_accessibility;
    QTemporaryDir m_stateDirectory;
    KSharedConfig::Ptr m_state;
};

int main(int argc, char **argv)
{
    // QUICK_TEST_MAIN_WITH_SETUP, written out so that what the QML said
    // while the tests ran has a say in the result.
    QTEST_SET_MAIN_SOURCE_PATH
    Setup setup;
    const int failed = quick_test_main_with_setup(argc, argv, "reverblabr", QUICK_TEST_SOURCE_DIR, &setup);
    if (QmlErrorWatch::report() > 0) {
        return failed != 0 ? failed : 1;
    }
    return failed;
}

#include "tst_qml.moc"
