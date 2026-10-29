// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/pluginhostprocess.h"

#include "plugin/messages.h"
#include "plugin/shared.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <optional>
#include <string>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::pw
{

namespace
{

/// The descriptors the host finds its two channels on.
constexpr int kControlFd = 3;
constexpr int kSharedFd = 4;

/// How often the watchdog looks.
constexpr std::chrono::milliseconds kWatchInterval{5};
/// A look this long after the last means the watch itself was held up.
constexpr std::chrono::milliseconds kLongestLookGap{100};
/// How long a window or a list of presets is waited for.
constexpr std::chrono::milliseconds kAnswerWithin{15000};

} // namespace

QString defaultPluginHostProgram()
{
    if (const QString chosen = qEnvironmentVariable("REVERBLABR_PLUGIN_HOST"); !chosen.isEmpty()) {
        return chosen;
    }
    const QString installed = QStringLiteral(RB_PLUGIN_HOST_INSTALLED);
    return QFileInfo(installed).isExecutable() ? installed : QStringLiteral(RB_PLUGIN_HOST_BUILT);
}

class PluginHostProcess::Private
{
public:
    PluginHostProcess *q = nullptr;
    pid_t pid = -1;
    int control = -1;
    int pidfd = -1;
    std::optional<plugin::SharedMemory> shared;
    std::unique_ptr<QSocketNotifier> controlNotifier;
    std::unique_ptr<QSocketNotifier> exitNotifier;
    QTimer watchdog;
    QTimer killTimer;
    /// **An answer that never came** is taken as a refusal after this
    /// long, so a panel never waits on a host that has not said: a window
    /// a plugin takes long to make, or a host of another build that does
    /// not know the question.
    QTimer uiAnswer;
    QTimer presetsAnswer;
    std::function<std::uint64_t()> upstream;
    std::uint32_t stalledCycles = 3;
    std::chrono::milliseconds stalledFor{40};
    std::chrono::milliseconds stalledAtMost{1000};
    QElapsedTimer sinceLook;
    QElapsedTimer sinceBeat;
    std::uint64_t lastBeat = 0;
    std::uint64_t upstreamAtBeat = 0;
    bool hungSaid = false;
    bool ready = false;
    bool quitting = false;
    QString nodeName;
    std::uint32_t latency = 0;
    /// **Changes held back while a state loads**, by parameter, the
    /// latest of each, in the order they were first made. Sent through the
    /// ring when the host says the new plugin is the one running.
    bool loadingState = false;
    std::vector<plugin::ParamChange> heldBack;

    void sendHeldBack()
    {
        loadingState = false;
        for (const plugin::ParamChange &change : heldBack) {
            (void)plugin::pushParam(shared->block(), change);
        }
        heldBack.clear();
    }

    void closeChannels()
    {
        controlNotifier.reset();
        if (control >= 0) {
            ::close(control);
            control = -1;
        }
    }

    void onControl()
    {
        for (;;) {
            const std::optional<plugin::Message> message = plugin::receiveMessage(control, false);
            if (!message) {
                if (!plugin::wouldBlock()) {
                    // The host's end has gone; its exit says the rest.
                    closeChannels();
                }
                return;
            }
            handle(*message);
        }
    }

    void handle(const plugin::Message &message)
    {
        const QString payload = QString::fromStdString(message.payload);
        switch (message.type) {
        case plugin::MessageType::Ready: {
            nodeName = payload.section(u'\n', 0, 0);
            latency = payload.section(u'\n', 1, 1).toUInt();
            ready = true;
            Q_EMIT q->ready(nodeName, latency);
            break;
        }
        case plugin::MessageType::Latency:
            latency = payload.toUInt();
            Q_EMIT q->latencyChanged(latency);
            break;
        case plugin::MessageType::State:
            Q_EMIT q->stateReceived(payload);
            break;
        case plugin::MessageType::StateLoaded:
            sendHeldBack();
            Q_EMIT q->stateLoaded(payload == u"ok"_s);
            break;
        case plugin::MessageType::Error:
            Q_EMIT q->failed(payload);
            break;
        case plugin::MessageType::UiOpened:
            uiAnswer.stop();
            Q_EMIT q->uiOpened();
            break;
        case plugin::MessageType::UiClosed:
            uiAnswer.stop();
            Q_EMIT q->uiClosed(payload);
            break;
        case plugin::MessageType::Refused:
            if (payload.toUInt() == static_cast<std::uint32_t>(plugin::MessageType::Presets)) {
                presetsAnswer.stop();
                Q_EMIT q->presetsRefused();
            }
            break;
        case plugin::MessageType::ParamChanged:
            if (const auto pair = plugin::parsePair(message.payload)) {
                if (const std::optional<float> value = plugin::parseValue(pair->second)) {
                    Q_EMIT q->paramChanged(QString::fromStdString(pair->first), *value);
                }
            }
            break;
        case plugin::MessageType::ParamGesture:
            if (const auto pair = plugin::parsePair(message.payload)) {
                Q_EMIT q->paramGesture(QString::fromStdString(pair->first), pair->second == "begin");
            }
            break;
        case plugin::MessageType::Values:
            if (const auto values = plugin::parseValues(message.payload)) {
                QList<QPair<QString, float>> list;
                for (const plugin::NamedValue &value : values->second) {
                    list.append({QString::fromStdString(value.id), value.value});
                }
                Q_EMIT q->valuesChanged(values->first == "edit", list);
            }
            break;
        case plugin::MessageType::Presets: {
            presetsAnswer.stop();
            QList<QPair<QString, QString>> list;
            for (const plugin::PresetEntry &preset : plugin::parsePresets(message.payload)) {
                list.append({QString::fromStdString(preset.id), QString::fromStdString(preset.name)});
            }
            Q_EMIT q->presetsListed(list);
            break;
        }
        case plugin::MessageType::PresetShown:
            Q_EMIT q->presetShown(payload);
            break;
        case plugin::MessageType::PresetSaved:
            Q_EMIT q->presetSaved(payload);
            break;
        default:
            break;
        }
    }

    void onExit()
    {
        int status = 0;
        const pid_t reaped = ::waitpid(pid, &status, WNOHANG);
        if (reaped != pid) {
            return;
        }
        exitNotifier.reset();
        ::close(pidfd);
        pidfd = -1;
        pid = -1;
        watchdog.stop();
        killTimer.stop();
        closeChannels();
        ready = false;
        const bool clean = WIFEXITED(status) && WEXITSTATUS(status) == 0 && quitting;
        Q_EMIT q->exited(WIFEXITED(status) ? WEXITSTATUS(status) : -1, !clean);
    }

    void onWatch()
    {
        if (!shared || !upstream || hungSaid) {
            return;
        }
        const std::uint64_t beat = shared->block().cycles.load(std::memory_order_acquire);
        const std::uint64_t now = upstream();
        // A watch held up itself is no evidence about the host.
        const bool held = sinceLook.isValid() && sinceLook.elapsed() > kLongestLookGap.count();
        sinceLook.start();
        if (beat != lastBeat || held) {
            lastBeat = beat;
            upstreamAtBeat = now;
            sinceBeat.start();
            return;
        }
        const bool afterUpstream = now - upstreamAtBeat >= stalledCycles && sinceBeat.elapsed() >= stalledFor.count();
        const bool tooLong = sinceBeat.elapsed() >= stalledAtMost.count();
        if (afterUpstream || tooLong) {
            hungSaid = true;
            Q_EMIT q->hung();
        }
    }
};

PluginHostProcess::PluginHostProcess(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->watchdog.setInterval(kWatchInterval);
    connect(&d->watchdog, &QTimer::timeout, this, [this] { d->onWatch(); });
    d->uiAnswer.setSingleShot(true);
    d->uiAnswer.setInterval(kAnswerWithin);
    connect(&d->uiAnswer, &QTimer::timeout, this, [this] { Q_EMIT uiClosed(u"failed"_s); });
    d->presetsAnswer.setSingleShot(true);
    d->presetsAnswer.setInterval(kAnswerWithin);
    connect(&d->presetsAnswer, &QTimer::timeout, this, [this] { Q_EMIT presetsRefused(); });
    d->killTimer.setSingleShot(true);
    connect(&d->killTimer, &QTimer::timeout, this, [this] {
        if (d->pid > 0) {
            ::kill(d->pid, SIGKILL);
        }
    });
}

PluginHostProcess::~PluginHostProcess()
{
    if (d->pid > 0) {
        ::kill(d->pid, SIGKILL);
        ::waitpid(d->pid, nullptr, 0);
    }
    if (d->pidfd >= 0) {
        ::close(d->pidfd);
    }
    d->closeChannels();
}

bool PluginHostProcess::start(const PluginHostSpec &spec)
{
    if (d->pid > 0) {
        return false;
    }
    std::expected<plugin::SharedMemory, plugin::SharedError> shared = plugin::SharedMemory::create();
    if (!shared) {
        return false;
    }
    int pair[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) {
        return false;
    }
    plugin::widenSocket(pair[0]);
    plugin::widenSocket(pair[1]);

    // Everything the child needs, made before fork: after it, only calls
    // that are safe in a copy of a threaded process.
    std::vector<std::string> arguments{spec.program.toStdString(),
                                       "--format",
                                       spec.format.toStdString(),
                                       "--plugin",
                                       spec.plugin.toStdString(),
                                       "--instance",
                                       spec.instance.toStdString(),
                                       "--control-fd",
                                       std::to_string(kControlFd),
                                       "--shm-fd",
                                       std::to_string(kSharedFd)};
    if (spec.monoMode == QStringLiteral("downmix")) {
        arguments.push_back("--mono");
        arguments.push_back("downmix");
    }
    if (spec.latencySamples > 0) {
        arguments.push_back("--latency");
        arguments.push_back(std::to_string(spec.latencySamples));
    }
    if (!spec.location.isEmpty()) {
        arguments.push_back("--location");
        arguments.push_back(spec.location.toStdString());
    }
    std::vector<char *> argv;
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    const int controlEnd = pair[1];
    const int sharedFd = shared->fd();

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pair[0]);
        ::close(pair[1]);
        return false;
    }
    if (pid == 0) {
        // **The host goes when the engine does**, even killed, so no
        // orphaned node is left in the graph.
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        // Out of the way first, in case either already sits on 3 or 4.
        const int control = ::fcntl(controlEnd, F_DUPFD, 10);
        const int memory = ::fcntl(sharedFd, F_DUPFD, 10);
        if (control < 0 || memory < 0 || ::dup2(control, kControlFd) < 0 || ::dup2(memory, kSharedFd) < 0) {
            ::_exit(127);
        }
        ::execv(argv[0], argv.data());
        ::_exit(127);
    }

    ::close(pair[1]);
    d->pid = pid;
    d->control = pair[0];
    ::fcntl(d->control, F_SETFL, ::fcntl(d->control, F_GETFL) | O_NONBLOCK);
    d->shared.emplace(std::move(*shared));
    d->ready = false;
    d->quitting = false;
    d->hungSaid = false;
    d->loadingState = false;
    d->heldBack.clear();
    d->nodeName.clear();

    d->controlNotifier = std::make_unique<QSocketNotifier>(d->control, QSocketNotifier::Read);
    connect(d->controlNotifier.get(), &QSocketNotifier::activated, this, [this] { d->onControl(); });
    d->pidfd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
    if (d->pidfd >= 0) {
        d->exitNotifier = std::make_unique<QSocketNotifier>(d->pidfd, QSocketNotifier::Read);
        connect(d->exitNotifier.get(), &QSocketNotifier::activated, this, [this] { d->onExit(); });
    }
    return true;
}

void PluginHostProcess::stop(std::chrono::milliseconds grace)
{
    if (d->pid <= 0) {
        return;
    }
    d->quitting = true;
    if (d->control < 0 || !plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::Quit, {}})) {
        ::kill(d->pid, SIGKILL);
        return;
    }
    d->killTimer.start(grace);
}

void PluginHostProcess::kill()
{
    if (d->pid > 0) {
        ::kill(d->pid, SIGKILL);
    }
}

bool PluginHostProcess::isRunning() const
{
    return d->pid > 0;
}

bool PluginHostProcess::isReady() const
{
    return d->ready;
}

int PluginHostProcess::pid() const
{
    return d->pid;
}

QString PluginHostProcess::nodeName() const
{
    return d->nodeName;
}

std::uint32_t PluginHostProcess::latencySamples() const
{
    return d->latency;
}

bool PluginHostProcess::setParam(std::uint32_t index, float value)
{
    if (!d->shared) {
        return false;
    }
    if (d->loadingState) {
        const auto held = std::ranges::find_if(
            d->heldBack, [index](const plugin::ParamChange &change) { return change.index == index; });
        if (held != d->heldBack.end()) {
            held->value = value;
        } else {
            d->heldBack.push_back(plugin::ParamChange{index, value});
        }
        return true;
    }
    return plugin::pushParam(d->shared->block(), plugin::ParamChange{index, value});
}

std::uint64_t PluginHostProcess::droppedParams() const
{
    return d->shared ? d->shared->block().paramsDropped.load(std::memory_order_relaxed) : 0;
}

std::uint64_t PluginHostProcess::cycles() const
{
    return d->shared ? d->shared->block().cycles.load(std::memory_order_acquire) : 0;
}

void PluginHostProcess::requestState()
{
    if (d->control >= 0) {
        (void)plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::GetState, {}});
    }
}

std::optional<QString> PluginHostProcess::stateNow(std::chrono::milliseconds timeout)
{
    if (d->control < 0 || !plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::GetState, {}})) {
        return std::nullopt;
    }
    QElapsedTimer waited;
    waited.start();
    while (d->control >= 0) {
        const qint64 left = timeout.count() - waited.elapsed();
        if (left <= 0) {
            return std::nullopt;
        }
        pollfd readable{d->control, POLLIN, 0};
        if (::poll(&readable, 1, static_cast<int>(left)) <= 0) {
            continue; // a signal, or the time is up: the loop says which
        }
        const std::optional<plugin::Message> message = plugin::receiveMessage(d->control, false);
        if (!message) {
            if (!plugin::wouldBlock()) {
                d->closeChannels();
            }
            continue;
        }
        if (message->type == plugin::MessageType::State) {
            return QString::fromStdString(message->payload);
        }
        if (message->type == plugin::MessageType::Refused
            && message->payload == std::to_string(static_cast<std::uint32_t>(plugin::MessageType::State))) {
            return std::nullopt; // a state too large to send: the last one stands
        }
        // Anything else that arrived meanwhile is said as it would have been.
        d->handle(*message);
    }
    return std::nullopt;
}

void PluginHostProcess::setState(const QString &state)
{
    if (d->control >= 0
        && plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::SetState, state.toStdString()})) {
        d->loadingState = true;
        return;
    }
    // Said, rather than lost: the plugin before it runs on.
    QMetaObject::invokeMethod(this, [this] { Q_EMIT stateLoaded(false); }, Qt::QueuedConnection);
}

void PluginHostProcess::openUi()
{
    if (d->control >= 0 && plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::OpenUi, {}})) {
        d->uiAnswer.start();
    } else {
        Q_EMIT uiClosed(u"failed"_s);
    }
}

void PluginHostProcess::closeUi()
{
    if (d->control >= 0) {
        (void)plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::CloseUi, {}});
    }
}

void PluginHostProcess::listPresets()
{
    if (d->control >= 0 && plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::ListPresets, {}})) {
        d->presetsAnswer.start();
    } else {
        Q_EMIT presetsRefused();
    }
}

void PluginHostProcess::loadPreset(const QString &presetId)
{
    if (d->control >= 0) {
        (void)plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::LoadPreset, presetId.toStdString()});
    }
}

void PluginHostProcess::savePreset(const QString &name)
{
    if (d->control >= 0) {
        (void)plugin::sendMessage(d->control, plugin::Message{plugin::MessageType::SavePreset, name.toStdString()});
    }
}

void PluginHostProcess::watch(std::function<std::uint64_t()> upstreamCycles,
                              std::uint32_t stalledCycles,
                              std::chrono::milliseconds stalledFor,
                              std::chrono::milliseconds stalledAtMost)
{
    d->upstream = std::move(upstreamCycles);
    d->stalledCycles = std::max<std::uint32_t>(stalledCycles, 1);
    d->stalledFor = stalledFor;
    d->stalledAtMost = stalledAtMost;
    d->sinceLook.invalidate();
    d->sinceBeat.start();
    d->lastBeat = cycles();
    d->upstreamAtBeat = d->upstream ? d->upstream() : 0;
    d->hungSaid = false;
    d->watchdog.start();
}

void PluginHostProcess::unwatch()
{
    d->watchdog.stop();
    d->upstream = nullptr;
}

} // namespace rb::pw
