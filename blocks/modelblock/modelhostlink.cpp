// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/modelblock/modelhostlink.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace rb::blocks
{

namespace
{

using Clock = std::chrono::steady_clock;

/// Where the host finds its two channels.
constexpr int kControlFd = 3;
constexpr int kSharedFd = 4;
/// What the host exits with when it has put its reason in the memory.
constexpr int kExitRefused = 3;

constexpr auto kInspectWithin = 5s;
/// How long a host may take over one model's loading.
constexpr auto kLoadWithin = 120s;
/// A host whose heart stands still this long, with sound going in, has hung.
constexpr auto kStillAtMost = 10s;
/// How long a host is given to leave by itself.
constexpr auto kLeaveWithin = 300ms;
/// Three crashes in this long, and it is left alone.
constexpr auto kCrashWindow = 60s;
constexpr std::size_t kMostCrashes = 3;
/// How often the watch looks, between events.
constexpr int kLookMs = 100;

[[nodiscard]] bool isExecutable(const std::string &path)
{
    return ::access(path.c_str(), X_OK) == 0;
}

/// Whether \a fd has something to read within \a ms.
[[nodiscard]] bool readable(int fd, int ms)
{
    pollfd entry{fd, POLLIN, 0};
    return ::poll(&entry, 1, ms) > 0;
}

} // namespace

std::string defaultModelHostProgram()
{
    if (const char *chosen = std::getenv("REVERBLABR_MODEL_HOST"); chosen != nullptr && *chosen != '\0') {
        return chosen;
    }
    const std::string installed{RB_MODEL_HOST_INSTALLED};
    return isExecutable(installed) ? installed : std::string{RB_MODEL_HOST_BUILT};
}

model::Description inspectFolder(const std::string &program, const std::string &folder)
{
    model::Description failed;
    failed.refusal = model::Refusal::NoHost;
    int pipeEnds[2] = {-1, -1};
    if (::pipe2(pipeEnds, O_CLOEXEC) != 0) {
        return failed;
    }
    std::array<const char *, 4> argv{program.c_str(), "--inspect", folder.c_str(), nullptr};
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipeEnds[0]);
        ::close(pipeEnds[1]);
        return failed;
    }
    if (child == 0) {
        // Only calls that are safe in a copy of a threaded process.
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        ::dup2(pipeEnds[1], STDOUT_FILENO);
        ::execv(argv[0], const_cast<char *const *>(argv.data()));
        ::_exit(127);
    }
    ::close(pipeEnds[1]);

    std::string text;
    const Clock::time_point deadline = Clock::now() + kInspectWithin;
    std::array<char, 1024> chunk{};
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0 || !readable(pipeEnds[0], static_cast<int>(left.count()))) {
            ::kill(child, SIGKILL);
            text.clear();
            break;
        }
        const ssize_t got = ::read(pipeEnds[0], chunk.data(), chunk.size());
        if (got <= 0) {
            break;
        }
        text.append(chunk.data(), static_cast<std::size_t>(got));
        if (text.size() > chunk.size() * 16) {
            ::kill(child, SIGKILL);
            text.clear();
            break;
        }
    }
    ::close(pipeEnds[0]);
    int status = 0;
    ::waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        // A reader the folder broke: the folder is what it could not read.
        failed.refusal = WIFSIGNALED(status) ? model::Refusal::NotAModel : model::Refusal::NoHost;
        return failed;
    }
    return model::parseDescriptionText(text);
}

ModelHostLink::ModelHostLink(model::SharedRings &rings, ModelHostSpec spec)
    : m_rings(rings)
    , m_spec(std::move(spec))
    , m_thread([this](const std::stop_token &stop) { run(stop); })
{
}

ModelHostLink::~ModelHostLink()
{
    m_thread.request_stop();
    m_thread.join();
}

bool ModelHostLink::spawn()
{
    int pair[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) {
        return false;
    }
    std::vector<std::string> arguments{m_spec.program,
                                       "--run",
                                       "--folder",
                                       m_spec.folder,
                                       "--rate",
                                       std::to_string(m_spec.sampleRate),
                                       "--latency",
                                       std::to_string(m_spec.latency),
                                       "--mono",
                                       m_spec.downmix ? "downmix" : "dualMono",
                                       "--control-fd",
                                       std::to_string(kControlFd),
                                       "--shm-fd",
                                       std::to_string(kSharedFd)};
    std::vector<char *> argv;
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    const int controlEnd = pair[1];
    const int sharedFd = m_rings.fd();

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pair[0]);
        ::close(pair[1]);
        return false;
    }
    if (child == 0) {
        // **Goes when this thread does**, which is at the latest when the
        // engine does, however that ends. Only calls that are safe in a copy
        // of a threaded process.
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        const int control = ::fcntl(controlEnd, F_DUPFD, 10);
        const int memory = ::fcntl(sharedFd, F_DUPFD, 10);
        if (control < 0 || memory < 0 || ::dup2(control, kControlFd) < 0 || ::dup2(memory, kSharedFd) < 0) {
            ::_exit(127);
        }
        ::execv(argv[0], argv.data());
        ::_exit(127);
    }
    ::close(pair[1]);
    m_control = pair[0];
    m_child = child;
    m_pidfd = static_cast<int>(::syscall(SYS_pidfd_open, child, 0));
    m_pid.store(child, std::memory_order_relaxed);
    return true;
}

bool ModelHostLink::reap() noexcept
{
    int status = 0;
    ::waitpid(m_child, &status, 0);
    m_child = -1;
    m_pid.store(-1, std::memory_order_relaxed);
    if (m_pidfd >= 0) {
        ::close(m_pidfd);
        m_pidfd = -1;
    }
    if (m_control >= 0) {
        ::close(m_control);
        m_control = -1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == kExitRefused;
}

void ModelHostLink::endHost() noexcept
{
    if (m_control >= 0) {
        // Its end of the socket closing is what tells a host to go.
        ::close(m_control);
        m_control = -1;
    }
    if (m_child < 0) {
        return;
    }
    if (m_pidfd < 0 || !readable(m_pidfd, static_cast<int>(kLeaveWithin.count()))) {
        ::kill(m_child, SIGKILL);
    }
    (void)reap();
}

ModelHostLink::Ended ModelHostLink::watch(const std::stop_token &stop)
{
    const model::SharedHeader &header = m_rings.header();
    const Clock::time_point started = Clock::now();
    Clock::time_point moved = started;
    std::uint64_t lastBeat = header.beat.load(std::memory_order_relaxed);
    std::uint64_t lastWritten = header.written.load(std::memory_order_relaxed);
    while (!stop.stop_requested()) {
        if (m_pidfd >= 0 && readable(m_pidfd, kLookMs)) {
            return reap() ? Ended::Refused : Ended::Crashed;
        }
        const Clock::time_point now = Clock::now();
        if (m_pidfd < 0) {
            // No pidfd: a look with waitpid, which this kernel never needs.
            std::this_thread::sleep_for(std::chrono::milliseconds{kLookMs});
            if (::waitpid(m_child, nullptr, WNOHANG) != 0) {
                m_child = -1;
                m_pid.store(-1, std::memory_order_relaxed);
                if (m_control >= 0) {
                    ::close(m_control);
                    m_control = -1;
                }
                return Ended::Crashed;
            }
        }
        const auto phase = static_cast<model::Phase>(header.phase.load(std::memory_order_acquire));
        if (phase != model::Phase::Ready && phase != model::Phase::Failed) {
            if (now - started > kLoadWithin) {
                m_failure.store(model::Refusal::LoadTooLong, std::memory_order_relaxed);
                ::kill(m_child, SIGKILL);
                (void)reap();
                return Ended::Stopped;
            }
            continue;
        }
        m_restarting.store(false, std::memory_order_relaxed);
        const std::uint64_t beat = header.beat.load(std::memory_order_relaxed);
        const std::uint64_t written = header.written.load(std::memory_order_relaxed);
        if (beat != lastBeat || written == lastWritten) {
            // Moving, or there is nothing it should be doing: a session
            // stopped is no hang.
            lastBeat = beat;
            lastWritten = written;
            moved = now;
        } else if (now - moved > kStillAtMost) {
            ::kill(m_child, SIGKILL);
            (void)reap();
            return Ended::Crashed;
        }
    }
    return Ended::Stopped;
}

void ModelHostLink::run(const std::stop_token &stop)
{
    std::deque<Clock::time_point> crashes;
    const auto waitForTryAgain = [this, &stop, &crashes] {
        while (!stop.stop_requested() && !m_tryAgain.exchange(false, std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{kLookMs});
        }
        crashes.clear();
        m_failure.store(model::Refusal::None, std::memory_order_relaxed);
    };

    while (!stop.stop_requested()) {
        model::SharedHeader &header = m_rings.header();
        header.reason.store(0, std::memory_order_relaxed);
        header.phase.store(static_cast<std::uint32_t>(model::Phase::Starting), std::memory_order_release);
        if (!spawn()) {
            m_failure.store(model::Refusal::NoHost, std::memory_order_relaxed);
            waitForTryAgain();
            continue;
        }
        const Ended ended = watch(stop);
        if (stop.stop_requested()) {
            break;
        }
        if (ended == Ended::Refused) {
            // Always the same answer for the same folder: nothing to retry.
            const std::uint32_t reason = header.reason.load(std::memory_order_acquire);
            const bool known = reason > 0 && reason <= static_cast<std::uint32_t>(model::kLastRefusal);
            m_failure.store(known ? static_cast<model::Refusal>(reason) : model::Refusal::RuntimeRefused,
                            std::memory_order_relaxed);
            m_restarting.store(false, std::memory_order_relaxed);
            waitForTryAgain();
            continue;
        }
        if (ended == Ended::Stopped) {
            m_restarting.store(false, std::memory_order_relaxed);
            waitForTryAgain();
            continue;
        }
        const Clock::time_point now = Clock::now();
        crashes.push_back(now);
        while (!crashes.empty() && now - crashes.front() > kCrashWindow) {
            crashes.pop_front();
        }
        if (crashes.size() >= kMostCrashes) {
            m_failure.store(model::Refusal::KeptCrashing, std::memory_order_relaxed);
            m_restarting.store(false, std::memory_order_relaxed);
            waitForTryAgain();
            continue;
        }
        m_restarting.store(true, std::memory_order_relaxed);
        const Clock::time_point resume = now + 500ms * static_cast<int>(crashes.size());
        while (!stop.stop_requested() && Clock::now() < resume) {
            std::this_thread::sleep_for(20ms);
        }
    }
    endHost();
}

} // namespace rb::blocks
