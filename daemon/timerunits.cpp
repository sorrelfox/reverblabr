// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "timerunits.h"

using namespace Qt::StringLiterals;

namespace rb::daemon::timerunits
{

namespace
{

/// A session's name as a line of a unit file: one line, and no `%`
/// systemd would read as a specifier. A backslash could end the line with
/// a continuation, so it is not kept either.
QString asDescription(const QString &sessionName)
{
    QString line;
    line.reserve(sessionName.size());
    for (const QChar character : sessionName) {
        if (character.category() == QChar::Other_Control || character == u' ' || character == u' ') {
            line += u' ';
        } else if (character == u'\\') {
            line += u'/';
        } else if (character == u'%') {
            line += u"%%"_s;
        } else {
            line += character;
        }
    }
    return line.simplified();
}

/// A path as the first word of ExecStart=: quoted, with what systemd would
/// expand or unescape inside the quotes escaped.
QString asCommandWord(const QString &path)
{
    QString word = path;
    word.replace(u'\\', u"\\\\"_s);
    word.replace(u'"', u"\\\""_s);
    word.replace(u'%', u"%%"_s);
    word.replace(u'$', u"$$"_s);
    return u'"' + word + u'"';
}

QString header(const QString &sessionName)
{
    return QLatin1StringView(kWrittenBy)
        + u"\n"
          u"# A timer of the session \"%1\". Reverblabr rewrites or removes this file\n"
          u"# whenever the timer changes: change it in Reverblabr's Schedule tab.\n"_s.arg(asDescription(sessionName));
}

} // namespace

QString baseName(const QString &timerId)
{
    return QLatin1StringView(kPrefix) + timerId;
}

QString timerIdOf(const QString &fileName)
{
    const QLatin1StringView prefix(kPrefix);
    if (!fileName.startsWith(prefix)) {
        return {};
    }
    QString id = fileName.mid(prefix.size());
    if (id.endsWith(".timer"_L1)) {
        id.chop(6);
    } else if (id.endsWith(".service"_L1)) {
        id.chop(8);
    } else {
        return {};
    }
    return core::timerIdIsValid(id) ? id : QString();
}

QString timerUnit(const core::Timer &timer, const QString &sessionName)
{
    // **AccuracySec=1s**: systemd otherwise lets a timer elapse up to a
    // minute late to save wake-ups, and a recording set for 14:30 should
    // start at 14:30. **Persistent=false**: a time that passed while the
    // machine was off is not made up for at boot, hours late.
    return header(sessionName)
        + u"\n[Unit]\n"
          u"Description=Reverblabr timer: %1\n"
          u"\n[Timer]\n"
          u"OnCalendar=%2\n"
          u"AccuracySec=1s\n"
          u"Persistent=false\n"
          u"Unit=%3.service\n"
          u"\n[Install]\n"
          u"WantedBy=timers.target\n"_s.arg(asDescription(sessionName), core::onCalendar(timer), baseName(timer.id));
}

QString serviceUnit(const core::Timer &timer, const QString &sessionName, const QString &ctlPath)
{
    // **Oneshot**: the command asks the engine and returns once the
    // session is starting; the engine, started by the bus if it is not
    // running, does the rest and stops the session when the time is up.
    // **--scheduled** has the engine refuse a fire that comes long after
    // its time, as after a sleep; **--** ends the options before the id.
    return header(sessionName)
        + u"\n[Unit]\n"
          u"Description=Reverblabr timer: %1\n"
          u"\n[Service]\n"
          u"Type=oneshot\n"
          u"ExecStart=%2 timer-fire --scheduled -- %3\n"_s.arg(asDescription(sessionName), asCommandWord(ctlPath), timer.id);
}

} // namespace rb::daemon::timerunits
