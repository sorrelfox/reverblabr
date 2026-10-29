// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// reverblabr-ctl: sessions started, stopped and changed from a terminal, a
// script, cron or a desktop tool. A client of the engine's public
// D-Bus API (docs/dbus-api.md) and nothing more: QtCore and QtDBus, no GUI
// and no KDE Frameworks, so it runs wherever there is a session bus.
//
//   reverblabr-ctl [--json] [--timeout SECONDS] [--no-activate] COMMAND …
//
// docs/ctl.md and the man page describe the commands, the JSON each prints
// and the exit codes.

#include "buildversion.h"
#include "commands.h"
#include "engineclient.h"
#include "translate.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QJsonDocument>

#include <chrono>
#include <cstdio>
#include <optional>

using namespace rb::ctl;
using namespace Qt::StringLiterals;

namespace
{

struct Options {
    bool json = false;
    bool activate = true;
    std::chrono::milliseconds timeout{10000};
    QString command;
    QStringList arguments;
};

void write(std::FILE *stream, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
}

[[nodiscard]] QString usage()
{
    return ctlTr("@info:usage",
                 "Usage: reverblabr-ctl [--json] [--timeout SECONDS] [--no-activate] COMMAND\n"
                 "\n"
                 "Commands:\n"
                 "  list                              the sessions' names, one a line\n"
                 "  status [SESSION]                  every session's state, or one's\n"
                 "  start SESSION                     run a session\n"
                 "  stop SESSION                      stop a session\n"
                 "  split SESSION [RECORDER]          start a new file in a running session's Recorders\n"
                 "  switch SESSION [SWITCH] A|B       turn an Input Switch to A or B\n"
                 "  set SESSION BLOCK SETTING VALUE   change one setting of a block\n"
                 "  timers list [SESSION]             every session's timers, or one's\n"
                 "  timer-fire TIMER                  do now what a timer does when its time comes\n"
                 "\n"
                 "A session is named by its name or its id, a block by its name or its id.\n"
                 "  --json            print the answer as JSON, the same in every language\n"
                 "  --timeout SECONDS how long to wait for the engine (10)\n"
                 "  --no-activate     only ask an engine that is already running\n")
        + u"\n"_s;
}

/// The command line, or why it cannot be read.
[[nodiscard]] Result<Options> parse(const QStringList &arguments)
{
    Options options;
    QStringList positional;
    bool optionsEnded = false;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString &argument = arguments.at(index);
        if (optionsEnded || !argument.startsWith(u'-') || argument == "-"_L1) {
            positional.append(argument);
            continue;
        }
        if (argument == "--"_L1) {
            optionsEnded = true;
        } else if (argument == "--json"_L1) {
            options.json = true;
        } else if (argument == "--no-activate"_L1) {
            options.activate = false;
        } else if (argument == "--help"_L1 || argument == "-h"_L1) {
            options.command = u"help"_s;
        } else if (argument == "--version"_L1) {
            options.command = u"version"_s;
        } else if (argument == "--timeout"_L1 || argument.startsWith("--timeout="_L1)) {
            QString value;
            if (argument.contains(u'=')) {
                value = argument.section(u'=', 1);
            } else if (index + 1 < arguments.size()) {
                value = arguments.at(++index);
            }
            bool ok = false;
            const double seconds = value.toDouble(&ok);
            if (!ok || seconds <= 0.0) {
                return fail(
                    ExitCode::Usage,
                    u"ctl.usage"_s,
                    {{"message"_L1, ctlTr("@info", "--timeout takes a number of seconds, not “%1”.").arg(value)}});
            }
            options.timeout = std::chrono::milliseconds(static_cast<qint64>(seconds * 1000.0));
        } else {
            return fail(ExitCode::Usage,
                        u"ctl.usage"_s,
                        {{"message"_L1, ctlTr("@info", "There is no option %1.").arg(argument)}});
        }
    }
    if (options.command.isEmpty()) {
        if (positional.isEmpty()) {
            return fail(ExitCode::Usage, u"ctl.usage"_s, {{"message"_L1, usage()}});
        }
        options.command = positional.takeFirst();
    }
    options.arguments = positional;
    return options;
}

/// How many arguments each command takes, at least and at most.
[[nodiscard]] std::optional<std::pair<qsizetype, qsizetype>> arity(const QString &command)
{
    if (command == "list"_L1) {
        return std::pair{0, 0};
    }
    if (command == "status"_L1) {
        return std::pair{0, 1};
    }
    if (command == "start"_L1 || command == "stop"_L1) {
        return std::pair{1, 1};
    }
    if (command == "split"_L1) {
        return std::pair{1, 2};
    }
    if (command == "switch"_L1) {
        return std::pair{2, 3};
    }
    if (command == "set"_L1) {
        return std::pair{4, 4};
    }
    if (command == "timers"_L1) {
        return std::pair{1, 2};
    }
    if (command == "timer-fire"_L1) {
        return std::pair{1, 1};
    }
    return std::nullopt;
}

[[nodiscard]] Result<Outcome> run(EngineClient &engine, const Options &options)
{
    const QStringList &a = options.arguments;
    const QString &command = options.command;
    if (command == "list"_L1) {
        return listCommand(engine);
    }
    if (command == "status"_L1) {
        return statusCommand(engine, a.value(0));
    }
    if (command == "start"_L1) {
        return startCommand(engine, a.at(0));
    }
    if (command == "stop"_L1) {
        return stopCommand(engine, a.at(0));
    }
    if (command == "split"_L1) {
        return splitCommand(engine, a.at(0), a.value(1));
    }
    if (command == "switch"_L1) {
        return a.size() == 3 ? switchCommand(engine, a.at(0), a.at(1), a.at(2))
                             : switchCommand(engine, a.at(0), QString(), a.at(1));
    }
    if (command == "timers"_L1) {
        return timersCommand(engine, a.value(1));
    }
    if (command == "timer-fire"_L1) {
        return timerFireCommand(engine, a.at(0));
    }
    return setCommand(engine, a.at(0), a.at(1), a.at(2), a.at(3));
}

/// Says why, in the form asked for, and returns the exit code.
int report(const Failure &failure, bool json)
{
    if (json) {
        QJsonObject error{{"code"_L1, failure.code}, {"args"_L1, failure.args}};
        if (failure.code == "ctl.usage"_L1) {
            // The message is for a person, and --json output is never
            // translated.
            error.insert("args"_L1, QJsonObject{});
        }
        write(stdout,
              QString::fromUtf8(QJsonDocument(QJsonObject{{"error"_L1, error}}).toJson(QJsonDocument::Compact))
                  + u"\n"_s);
    } else {
        QString message = describe(failure);
        if (!message.endsWith(u'\n')) {
            message += u'\n';
        }
        write(stderr, failure.code == "ctl.usage"_L1 ? message : u"reverblabr-ctl: "_s + message);
    }
    return static_cast<int>(failure.exit);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(u"reverblabr-ctl"_s);
    QCoreApplication::setApplicationVersion(QString::fromUtf8(rb::versionFull()));
    setUpTranslations();

    const QStringList arguments = QCoreApplication::arguments();
    const bool json = arguments.contains("--json"_L1);
    const Result<Options> options = parse(arguments);
    if (!options) {
        return report(options.error(), json);
    }
    if (options->command == "help"_L1) {
        write(stdout, usage());
        return static_cast<int>(ExitCode::Ok);
    }
    if (options->command == "version"_L1) {
        write(stdout, u"reverblabr-ctl "_s + QCoreApplication::applicationVersion() + u"\n"_s);
        return static_cast<int>(ExitCode::Ok);
    }
    const std::optional<std::pair<qsizetype, qsizetype>> counts = arity(options->command);
    if (!counts) {
        return report(
            {ExitCode::Usage,
             u"ctl.usage"_s,
             {{"message"_L1,
               QString(ctlTr("@info", "There is no command “%1”.").arg(options->command) + u"\n\n"_s + usage())}}},
            options->json);
    }
    // `timers` has one thing to do so far, and says so: a later one is a
    // new word after it rather than a new command.
    if (options->arguments.size() < counts->first || options->arguments.size() > counts->second
        || (options->command == "timers"_L1 && options->arguments.constFirst() != "list"_L1)) {
        return report({ExitCode::Usage, u"ctl.usage"_s, {{"message"_L1, usage()}}}, options->json);
    }

    EngineClient engine(QDBusConnection::sessionBus(), options->timeout);
    if (Result<void> reached = engine.reach(options->activate); !reached) {
        return report(reached.error(), options->json);
    }
    const Result<Outcome> outcome = run(engine, *options);
    if (!outcome) {
        return report(outcome.error(), options->json);
    }
    if (options->json) {
        write(stdout, QString::fromUtf8(QJsonDocument(outcome->json).toJson(QJsonDocument::Compact)) + u"\n"_s);
    } else if (!outcome->lines.isEmpty()) {
        write(stdout, outcome->lines.join(u'\n') + u"\n"_s);
    }
    return static_cast<int>(ExitCode::Ok);
}
