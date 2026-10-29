// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "commands.h"

#include "engineclient.h"
#include "lookup.h"
#include "translate.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>

#include <functional>

using namespace Qt::StringLiterals;

namespace rb::ctl
{

namespace
{

constexpr auto kRecorderType = "output.recorder"_L1;
constexpr auto kInputSwitchType = "advanced.inputswitch"_L1;
constexpr auto kClosed = "closed"_L1;
constexpr auto kRunning = "running"_L1;

[[nodiscard]] QJsonObject sessionJson(const SessionEntry &session)
{
    return {{"id"_L1, session.id}, {"name"_L1, session.name}, {"state"_L1, session.state}};
}

[[nodiscard]] Result<std::vector<SessionEntry>> sessions(EngineClient &engine)
{
    const Result<QJsonArray> listed = engine.listSessions();
    if (!listed) {
        return std::unexpected(listed.error());
    }
    return sessionsFromJson(*listed);
}

[[nodiscard]] Result<SessionEntry> session(EngineClient &engine, const QString &wanted)
{
    const Result<std::vector<SessionEntry>> all = sessions(engine);
    if (!all) {
        return std::unexpected(all.error());
    }
    return findSession(*all, wanted);
}

/// The name a person knows a block by.
[[nodiscard]] QString blockName(const BlockEntry &block)
{
    return block.nickname.isEmpty() ? block.id : block.nickname;
}

/// Opens a session only on disk for as long as `work` takes, and closes it
/// again, so a setting changed from a script lands in the file and the
/// session is left as it was found.
///
/// **Opening it is heard by every window**, which gets a tab for it until it
/// closes again; that is the price of changing a session nobody has open,
/// and a script that minds opens it in the window first.
template <typename Work>
[[nodiscard]] Result<Outcome> withSessionOpen(EngineClient &engine, SessionEntry &entry, Work work)
{
    const bool opened = entry.state == kClosed;
    if (opened) {
        if (Result<void> open = engine.openSession(entry.id); !open) {
            return std::unexpected(open.error());
        }
    }
    Result<Outcome> done = work();
    if (opened) {
        Result<void> closed = engine.closeSession(entry.id, entry.name);
        if (done && !closed) {
            return std::unexpected(closed.error());
        }
    }
    return done;
}

/// How long a timer runs, as a person says it.
[[nodiscard]] QString durationWords(int seconds)
{
    const int minutes = seconds / 60;
    if (minutes == 0) {
        return ctlTr("@info a timer's duration in seconds", "%1 s").arg(seconds);
    }
    if (minutes < 60) {
        return ctlTr("@info a timer's duration in minutes", "%1 min").arg(minutes);
    }
    if (minutes % 60 == 0) {
        return ctlTr("@info a timer's duration in hours", "%1 h").arg(minutes / 60);
    }
    return ctlTr("@info a timer's duration in hours and minutes", "%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

/// When a timer comes round, as a person says it: "every day", "Mon, Thu".
[[nodiscard]] QString repeatWords(const QJsonObject &timer)
{
    const QString repeat = timer.value("repeat"_L1).toString();
    if (repeat == "once"_L1) {
        return ctlTr("@info a timer that runs once, on a date", "once on %1").arg(timer.value("date"_L1).toString());
    }
    if (repeat == "daily"_L1) {
        return ctlTr("@info a timer's days", "every day");
    }
    if (repeat == "weekdays"_L1) {
        return ctlTr("@info a timer's days", "Monday to Friday");
    }
    static const QStringList kDays{u"mon"_s, u"tue"_s, u"wed"_s, u"thu"_s, u"fri"_s, u"sat"_s, u"sun"_s};
    QStringList days;
    for (const QJsonValue &day : timer.value("days"_L1).toArray()) {
        days.append(
            QLocale::system().dayName(static_cast<int>(kDays.indexOf(day.toString())) + 1, QLocale::ShortFormat));
    }
    return days.join(", "_L1);
}

/// One timer on a line: its session, when, for how long, when next, and the
/// id `timer-fire` takes.
[[nodiscard]] QString timerLine(const QJsonObject &timer)
{
    const QString when = ctlTr("@info a timer: its days, its time and how long it runs", "%1 at %2 for %3")
                             .arg(repeatWords(timer),
                                  timer.value("time"_L1).toString(),
                                  durationWords(timer.value("duration"_L1).toInt()));
    QString next;
    if (!timer.value("enabled"_L1).toBool()) {
        next = ctlTr("@info a timer that is switched off", "off");
    } else if (timer.contains("endsAt"_L1)) {
        next = ctlTr("@info a timer running its session, and until when", "running until %1")
                   .arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timer.value("endsAt"_L1).toDouble()))
                            .toString(u"yyyy-MM-dd HH:mm"_s));
    } else if (timer.contains("nextRun"_L1)) {
        next = ctlTr("@info when a timer next starts its session", "next %1")
                   .arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timer.value("nextRun"_L1).toDouble()))
                            .toString(u"yyyy-MM-dd HH:mm"_s));
    } else {
        next = ctlTr("@info a timer that will not start its session again", "not again");
    }
    return ctlTr("@info session: a timer, when it next runs, and its id", "%1: %2, %3 (%4)")
        .arg(timer.value("sessionName"_L1).toString(), when, next, timer.value("id"_L1).toString());
}

} // namespace

QString stateWord(const QString &state)
{
    if (state == "idle"_L1) {
        return ctlTr("@info session state", "stopped");
    }
    if (state == "starting"_L1) {
        return ctlTr("@info session state", "starting");
    }
    if (state == kRunning) {
        return ctlTr("@info session state", "running");
    }
    if (state == "stopping"_L1) {
        return ctlTr("@info session state", "stopping");
    }
    if (state == "error"_L1) {
        return ctlTr("@info session state", "could not run");
    }
    if (state == kClosed) {
        return ctlTr("@info session state: on disk, not open", "closed");
    }
    return state;
}

Result<Outcome> listCommand(EngineClient &engine)
{
    const Result<std::vector<SessionEntry>> all = sessions(engine);
    if (!all) {
        return std::unexpected(all.error());
    }
    Outcome outcome;
    QJsonArray listed;
    for (const SessionEntry &entry : *all) {
        listed.append(sessionJson(entry));
        // One name a line and nothing else, for `while read` and completion.
        outcome.lines.append(entry.name);
    }
    outcome.json.insert("sessions"_L1, listed);
    return outcome;
}

Result<Outcome> statusCommand(EngineClient &engine, const QString &wanted)
{
    Outcome outcome;
    if (!wanted.isEmpty()) {
        const Result<SessionEntry> entry = session(engine, wanted);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        outcome.json.insert("session"_L1, sessionJson(*entry));
        outcome.lines.append(ctlTr("@info session and its state", "%1: %2").arg(entry->name, stateWord(entry->state)));
        return outcome;
    }
    const Result<std::vector<SessionEntry>> all = sessions(engine);
    if (!all) {
        return std::unexpected(all.error());
    }
    QJsonArray listed;
    for (const SessionEntry &entry : *all) {
        listed.append(sessionJson(entry));
        outcome.lines.append(ctlTr("@info session and its state", "%1: %2").arg(entry.name, stateWord(entry.state)));
    }
    if (all->empty()) {
        outcome.lines.append(ctlTr("@info", "There are no sessions."));
    }
    outcome.json.insert("sessions"_L1, listed);
    return outcome;
}

Result<Outcome> startCommand(EngineClient &engine, const QString &wanted)
{
    Result<SessionEntry> entry = session(engine, wanted);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    Outcome outcome;
    if (entry->state == kRunning) {
        outcome.json = {{"session"_L1, sessionJson(*entry)}, {"changed"_L1, false}};
        outcome.lines.append(ctlTr("@info", "“%1” is already running.").arg(entry->name));
        return outcome;
    }
    // **Opened first when only on disk**, and left open: the engine keeps a
    // running session open, and a window started later gets its tab.
    if (entry->state == kClosed) {
        if (Result<void> open = engine.openSession(entry->id); !open) {
            return std::unexpected(open.error());
        }
    }
    const Result<QString> state = engine.start(entry->id, entry->name);
    if (!state) {
        return std::unexpected(state.error());
    }
    entry->state = *state;
    outcome.json = {{"session"_L1, sessionJson(*entry)}, {"changed"_L1, true}};
    outcome.lines.append(ctlTr("@info", "Started “%1”.").arg(entry->name));
    return outcome;
}

Result<Outcome> stopCommand(EngineClient &engine, const QString &wanted)
{
    Result<SessionEntry> entry = session(engine, wanted);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    Outcome outcome;
    // Stopping what does not run is already done, not a failure: a script
    // that says "stop" wants it stopped.
    if (entry->state == kClosed || entry->state == "idle"_L1) {
        outcome.json = {{"session"_L1, sessionJson(*entry)}, {"changed"_L1, false}};
        outcome.lines.append(ctlTr("@info", "“%1” is not running.").arg(entry->name));
        return outcome;
    }
    const Result<QString> state = engine.stop(entry->id);
    if (!state) {
        return std::unexpected(state.error());
    }
    entry->state = *state;
    outcome.json = {{"session"_L1, sessionJson(*entry)}, {"changed"_L1, true}};
    outcome.lines.append(ctlTr("@info", "Stopped “%1”.").arg(entry->name));
    return outcome;
}

Result<Outcome> splitCommand(EngineClient &engine, const QString &wanted, const QString &block)
{
    const Result<SessionEntry> entry = session(engine, wanted);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    if (entry->state != kRunning) {
        return fail(ExitCode::Refused, u"ctl.notRunning"_s, {{"session"_L1, entry->name}});
    }
    const Result<QJsonObject> snapshot = engine.snapshot(entry->id);
    if (!snapshot) {
        return std::unexpected(snapshot.error());
    }
    const std::vector<BlockEntry> blocks = blocksFromSnapshot(*snapshot);
    std::vector<BlockEntry> recorders;
    if (block.isEmpty()) {
        for (const BlockEntry &candidate : blocks) {
            if (candidate.type == kRecorderType) {
                recorders.push_back(candidate);
            }
        }
        if (recorders.empty()) {
            return fail(ExitCode::NotFound,
                        u"ctl.blockNotFound"_s,
                        {{"block"_L1, QString()}, {"type"_L1, QString(kRecorderType)}});
        }
    } else {
        const Result<BlockEntry> recorder = findBlock(blocks, block, QString(kRecorderType));
        if (!recorder) {
            return std::unexpected(recorder.error());
        }
        recorders.push_back(*recorder);
    }

    Outcome outcome;
    QJsonArray split;
    for (const BlockEntry &recorder : recorders) {
        const Result<QString> file = engine.split(entry->id, recorder.id);
        if (!file) {
            return std::unexpected(file.error());
        }
        split.append(QJsonObject{{"block"_L1, recorder.id}, {"file"_L1, *file}});
        outcome.lines.append(ctlTr("@info", "“%1” started a new file: %2").arg(blockName(recorder), *file));
    }
    outcome.json = {{"session"_L1, sessionJson(*entry)}, {"split"_L1, split}};
    return outcome;
}

namespace
{

/// Sets one setting of a block the person named, in a session opened for
/// the purpose if it was only on disk.
[[nodiscard]] Result<Outcome> setting(EngineClient &engine,
                                      const QString &wanted,
                                      const QString &block,
                                      const QString &type,
                                      const QString &param,
                                      const QString &typed,
                                      const std::function<QString(const BlockEntry &, const QJsonValue &)> &said)
{
    Result<SessionEntry> entry = session(engine, wanted);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    return withSessionOpen(engine, *entry, [&]() -> Result<Outcome> {
        const Result<QJsonObject> snapshot = engine.snapshot(entry->id);
        if (!snapshot) {
            return std::unexpected(snapshot.error());
        }
        const Result<BlockEntry> target = findBlock(blocksFromSnapshot(*snapshot), block, type);
        if (!target) {
            return std::unexpected(target.error());
        }
        const Result<QJsonObject> catalogue = engine.catalogue();
        if (!catalogue) {
            return std::unexpected(catalogue.error());
        }
        const QJsonObject descriptor = paramDescriptor(*catalogue, target->type, param);
        QJsonValue value;
        if (!descriptor.isEmpty()) {
            Result<QJsonValue> checked = valueForParam(descriptor, typed);
            if (!checked) {
                return std::unexpected(checked.error());
            }
            value = *checked;
        } else if (target->type.startsWith("plugin."_L1)) {
            // A hosted plugin's settings are its own and not in the
            // catalogue; its host is what checks them.
            value = untypedValue(typed);
        } else {
            return fail(
                ExitCode::NotFound, u"ctl.paramNotFound"_s, {{"block"_L1, blockName(*target)}, {"param"_L1, param}});
        }
        if (Result<void> done = engine.setParam(entry->id, target->id, param, value); !done) {
            return std::unexpected(done.error());
        }
        Outcome outcome;
        outcome.json = {
            {"session"_L1, sessionJson(*entry)}, {"block"_L1, target->id}, {"param"_L1, param}, {"value"_L1, value}};
        outcome.lines.append(said(*target, value));
        return outcome;
    });
}

[[nodiscard]] QString valueText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

} // namespace

Result<Outcome> switchCommand(EngineClient &engine, const QString &wanted, const QString &block, const QString &input)
{
    Result<Outcome> done = setting(
        engine,
        wanted,
        block,
        QString(kInputSwitchType),
        u"input"_s,
        input,
        [](const BlockEntry &target, const QJsonValue &value) {
            return ctlTr("@info", "“%1” now takes input %2.").arg(blockName(target), value.toString().toUpper());
        });
    if (done) {
        // The same answer as `set`'s, and the input on its own for a script
        // that only switches.
        done->json.insert("input"_L1, done->json.value("value"_L1));
    }
    return done;
}

Result<Outcome> setCommand(EngineClient &engine,
                           const QString &wanted,
                           const QString &block,
                           const QString &param,
                           const QString &value)
{
    return setting(
        engine, wanted, block, QString(), param, value, [&param](const BlockEntry &target, const QJsonValue &set) {
            return ctlTr("@info setting, block, value", "Set %1 of “%2” to %3.")
                .arg(param, blockName(target), valueText(set));
        });
}

Result<Outcome> timersCommand(EngineClient &engine, const QString &wanted)
{
    QString sessionId;
    if (!wanted.isEmpty()) {
        const Result<SessionEntry> entry = session(engine, wanted);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        sessionId = entry->id;
    }
    const Result<QJsonArray> timers = engine.listTimers();
    if (!timers) {
        return std::unexpected(timers.error());
    }
    Outcome outcome;
    QJsonArray listed;
    for (const QJsonValue &value : *timers) {
        const QJsonObject timer = value.toObject();
        if (!sessionId.isEmpty() && timer.value("sessionId"_L1).toString() != sessionId) {
            continue;
        }
        listed.append(timer);
        outcome.lines.append(timerLine(timer));
    }
    if (listed.isEmpty()) {
        outcome.lines.append(ctlTr("@info", "There are no timers."));
    }
    outcome.json.insert("timers"_L1, listed);
    return outcome;
}

Result<Outcome> timerFireCommand(EngineClient &engine, const QString &timerId)
{
    const Result<QString> sessionId = engine.fireTimer(timerId);
    if (!sessionId) {
        return std::unexpected(sessionId.error());
    }
    Outcome outcome;
    outcome.json = {{"timer"_L1, timerId}, {"session"_L1, *sessionId}};
    // The session's name for the journal, when the list gives it; the
    // timer has fired either way.
    QString name = *sessionId;
    if (const Result<std::vector<SessionEntry>> all = sessions(engine); all) {
        for (const SessionEntry &entry : *all) {
            if (entry.id == *sessionId) {
                name = entry.name;
            }
        }
    }
    outcome.lines.append(ctlTr("@info", "The timer %1 started “%2”.").arg(timerId, name));
    return outcome;
}

QString describe(const Failure &failure)
{
    const QJsonObject &args = failure.args;
    const auto arg = [&args](const char *key) { return args.value(QLatin1StringView(key)).toVariant().toString(); };
    const auto list = [&args](const char *key) {
        QStringList items;
        for (const QJsonValue &item : args.value(QLatin1StringView(key)).toArray()) {
            items.append(item.toString());
        }
        return items.join(", "_L1);
    };
    const QString &code = failure.code;
    if (code == "ctl.sessionNotFound"_L1 || code == "session.notFound"_L1) {
        return ctlTr("@info", "There is no session called “%1”.").arg(arg("session"));
    }
    if (code == "ctl.sessionAmbiguous"_L1) {
        return ctlTr("@info", "More than one session is called “%1”. Name it by its id: %2")
            .arg(arg("session"), list("matches"));
    }
    if (code == "ctl.blockNotFound"_L1) {
        if (arg("block").isEmpty()) {
            return ctlTr("@info", "The session has no block of the kind this needs (%1).").arg(arg("type"));
        }
        return ctlTr("@info", "The session has no block called “%1”.").arg(arg("block"));
    }
    if (code == "ctl.blockAmbiguous"_L1) {
        if (arg("block").isEmpty()) {
            return ctlTr("@info", "The session has more than one block of that kind. Name one: %1")
                .arg(list("matches"));
        }
        return ctlTr("@info", "More than one block is called “%1”. Name it by its id: %2")
            .arg(arg("block"), list("matches"));
    }
    if (code == "ctl.paramNotFound"_L1) {
        return ctlTr("@info", "“%1” has no setting called %2.").arg(arg("block"), arg("param"));
    }
    if (code == "ctl.badValue"_L1) {
        if (args.contains("choices"_L1)) {
            return ctlTr("@info", "%1 cannot be “%2”. It takes one of: %3")
                .arg(arg("param"), arg("value"), list("choices"));
        }
        if (args.contains("minimum"_L1) && args.contains("maximum"_L1)) {
            return ctlTr("@info", "%1 cannot be “%2”. It takes a number from %3 to %4.")
                .arg(arg("param"), arg("value"), arg("minimum"), arg("maximum"));
        }
        return ctlTr("@info", "%1 cannot be “%2”.").arg(arg("param"), arg("value"));
    }
    if (code == "ctl.readOnly"_L1) {
        return ctlTr("@info", "%1 is something the block reports, and cannot be set.").arg(arg("param"));
    }
    if (code == "ctl.notRunning"_L1) {
        return ctlTr("@info", "“%1” is not running.").arg(arg("session"));
    }
    if (code == "ctl.unreachable"_L1) {
        const QString reason = arg("reason");
        if (reason == "noSessionBus"_L1) {
            return ctlTr("@info", "There is no session bus to reach the engine on.");
        }
        if (reason == "notRunning"_L1) {
            return ctlTr("@info", "The engine is not running.");
        }
        if (reason.startsWith("org.freedesktop.DBus.Error.Spawn."_L1)) {
            return ctlTr("@info", "The engine would not start (%1).").arg(reason);
        }
        return ctlTr("@info", "The engine is not installed, so the session bus cannot start it.");
    }
    if (code == "ctl.timeout"_L1) {
        return ctlTr("@info", "The engine did not answer %1 in time.").arg(arg("method"));
    }
    if (code == "ctl.usage"_L1) {
        return arg("message");
    }
    if (code == "ctl.dbusError"_L1) {
        return ctlTr("@info", "The engine did not take the call: %1").arg(arg("message"));
    }
    if (code == "session.cannotRun"_L1) {
        return ctlTr("@info", "“%1” could not start (%2).")
            .arg(arg("session"), arg("reason").isEmpty() ? arg("state") : arg("reason"));
    }
    if (code == "edit.refused"_L1) {
        return ctlTr("@info", "The engine refused the change (%1).").arg(arg("reason"));
    }
    if (code == "timer.notFound"_L1) {
        return ctlTr("@info", "There is no timer “%1”.").arg(arg("timer"));
    }
    if (code == "recording.refused"_L1) {
        return ctlTr("@info", "The recording could not be split (%1).").arg(arg("reason"));
    }
    // Any other of the engine's codes, as it sent it: the code is stable,
    // and the arguments say what it is about.
    return ctlTr("@info engine error code and its details", "The engine refused: %1 %2")
        .arg(code, QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
}

} // namespace rb::ctl
