// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/quitapplications.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

/// The server's own word on a client, which the client cannot change.
constexpr auto kSecurityPid = "pipewire.sec.pid"_L1;
constexpr auto kPortalAppId = "pipewire.access.portal.app_id"_L1;

const pw::PwClient *clientById(const QList<pw::PwClient> &clients, std::uint32_t id)
{
    const auto found =
        std::ranges::find_if(clients, [id](const pw::PwClient &client) { return client.object.id == id; });
    return found == clients.end() ? nullptr : &*found;
}

/// Closes a file descriptor when it goes.
class Descriptor
{
public:
    explicit Descriptor(int fd)
        : m_fd(fd)
    {
    }
    ~Descriptor()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;

    [[nodiscard]] int get() const noexcept
    {
        return m_fd;
    }

private:
    int m_fd = -1;
};

} // namespace

QList<QuitTarget> quitTargets(const QList<pw::PwNode> &streams, const QList<pw::PwClient> &clients)
{
    QList<QuitTarget> targets;
    for (const pw::PwNode &stream : streams) {
        const pw::PwClient *client = clientById(clients, stream.clientId);
        if (client == nullptr || client->isUs) {
            continue;
        }
        QuitTarget target;
        target.flatpakId = client->props.value(QString(kPortalAppId));
        if (target.flatpakId.isEmpty()) {
            bool isNumber = false;
            target.pid = client->props.value(QString(kSecurityPid)).toLongLong(&isNumber);
            if (!isNumber || target.pid <= 1) {
                continue;
            }
            target.binary = stream.applicationBinary;
        }
        if (!targets.contains(target)) {
            targets.append(target);
        }
    }
    return targets;
}

QuitResult quitProcess(qint64 pid, const QString &binary)
{
    if (pid <= 1 || pid == QCoreApplication::applicationPid() || binary.isEmpty()) {
        return QuitResult::WrongProgram;
    }
    // **The process, held**: what is checked below and what is signalled
    // are the same one, or the signal fails.
    const Descriptor pidfd(static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0)));
    if (pidfd.get() < 0) {
        return QuitResult::Gone;
    }
    const QString proc = u"/proc/%1"_s.arg(pid);
    // The process's own owner: /proc/<pid> belongs to it, where its exe is
    // a link to a file that belongs to whoever installed it.
    struct stat status{};
    if (::stat(QFile::encodeName(proc).constData(), &status) != 0) {
        return QuitResult::Gone;
    }
    if (status.st_uid != ::getuid()) {
        return QuitResult::NotOurs;
    }
    const QString exe = QFileInfo(QFile::symLinkTarget(proc + u"/exe"_s)).fileName();
    QFile comm(proc + u"/comm"_s);
    const QString command = comm.open(QIODevice::ReadOnly) ? QString::fromUtf8(comm.readAll()).trimmed() : QString();
    // The kernel keeps 15 characters of a command's name.
    if (exe != binary && (command.isEmpty() || command != binary.left(15))) {
        return QuitResult::WrongProgram;
    }
    if (::syscall(SYS_pidfd_send_signal, pidfd.get(), SIGTERM, nullptr, 0) != 0) {
        return QuitResult::Gone;
    }
    return QuitResult::Signalled;
}

} // namespace rb::core
