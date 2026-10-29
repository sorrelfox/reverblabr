// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// reverblabr-plugin-host (ADR-0014): one plugin, in a process of its own,
// as a PipeWire node of its own. The engine starts it, hands it a control
// socket and a block of shared memory, and links its node into the session;
// PipeWire runs it in the same cycle as the rest of the session. A plugin
// that crashes takes this process with it and nothing else.
//
//   reverblabr-plugin-host --format builtin --plugin test.gain --instance <id>
//                          --control-fd <fd> --shm-fd <fd> [--latency <samples>]
//                          [--mono dualMono|downmix] [--location <path>]

#include "buildversion.h"
#include "dsp/block.h"
#include "ladspaplugin.h"
#include "lv2plugin.h"
#ifdef RB_HAVE_CLAP
#include "clapplugin.h"
#endif
#include "plugin/hostprocessor.h"
#include "plugin/messages.h"
#include "plugin/shared.h"
#include "plugin/testplugins.h"
#include "scan.h"

#include <pipewire/filter.h>
#include <pipewire/pipewire.h>
#include <spa/node/io.h>
#include <spa/utils/result.h>

#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace rb;

namespace
{

/// What the command line said.
struct Options {
    std::string format;
    std::string plugin;
    std::string instance;
    int controlFd = -1;
    int sharedFd = -1;
    std::uint32_t latency = 0;
    /// What a mono plugin does on a stereo lane: "dualMono" or "downmix".
    std::string mono = "dualMono";
    /// Where the plugin is, for a format whose id does not say: the
    /// `.clap` file of a CLAP plugin.
    std::string location;
};

[[nodiscard]] std::optional<Options> parse(int argc, char **argv)
{
    Options options;
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::string_view key = argv[index];
        const std::string_view value = argv[index + 1];
        const auto number = [&value](auto &into) {
            return std::from_chars(value.data(), value.data() + value.size(), into).ec == std::errc{};
        };
        if (key == "--format") {
            options.format = value;
        } else if (key == "--plugin") {
            options.plugin = value;
        } else if (key == "--instance") {
            options.instance = value;
        } else if (key == "--control-fd") {
            if (!number(options.controlFd)) {
                return std::nullopt;
            }
        } else if (key == "--shm-fd") {
            if (!number(options.sharedFd)) {
                return std::nullopt;
            }
        } else if (key == "--mono") {
            options.mono = value;
        } else if (key == "--location") {
            options.location = value;
        } else if (key == "--latency") {
            if (!number(options.latency)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    if (options.format.empty() || options.plugin.empty() || options.instance.empty() || options.controlFd < 0
        || options.sharedFd < 0) {
        return std::nullopt;
    }
    return options;
}

/// What the host keeps with each of its ports: where the port's own end of
/// its buffers is, as the filter hands it over in `io_changed`. Set on the
/// main loop, read on the data thread, so atomic.
struct Port {
    std::atomic<spa_io_buffers *> io{nullptr};
};

/// Whether an output still holds what it wrote the cycle before, because the
/// server has not passed it on — as can happen in the cycle a port's buffers
/// are made again, after a quantum change. A buffer asked for now would be
/// queued behind it and that channel would play a cycle late for as long as
/// the link lasted, so the port gets none this cycle: the plugin
/// writes that channel somewhere else, and the buffer it holds goes out.
[[nodiscard]] bool stillUnread(const Port *port)
{
    const spa_io_buffers *io = port != nullptr ? port->io.load(std::memory_order_acquire) : nullptr;
    return io != nullptr && io->status == SPA_STATUS_HAVE_DATA;
}

/// The host: its node, the plugin in it, and the two channels to the engine.
struct Host {
    Options options;
    plugin::SharedMemory shared;
    plugin::HostProcessor processor;
    pw_main_loop *loop = nullptr;
    pw_filter *filter = nullptr;
    std::vector<Port *> inputs;
    std::vector<Port *> outputs;
    std::vector<float *> inputBuffers;
    std::vector<float *> outputBuffers;
    /// The plugin the data thread is running, owned here.
    std::unique_ptr<dsp::Block> block;
    /// LV2's world, loaded once, for a host of an LV2 plugin.
    std::shared_ptr<pluginhost::Lv2World> lv2;
#ifdef RB_HAVE_CLAP
    /// The CLAP file, loaded once, for a host of a CLAP plugin.
    std::shared_ptr<pluginhost::ClapLibrary> clap;
#endif
    /// The latency last told to the engine.
    std::uint32_t reportedLatency = 0;
    std::string nodeName;
    bool announced = false;

    explicit Host(Options given, plugin::SharedMemory memory)
        : options(std::move(given))
        , shared(std::move(memory))
        , processor(shared.block())
    {
    }

    void send(plugin::MessageType type, std::string payload = {})
    {
        (void)plugin::sendMessage(options.controlFd, plugin::Message{type, std::move(payload)});
    }

    /// A plugin made and prepared for \a rate, with \a state — the text a
    /// State message carries — put back.
    [[nodiscard]] std::unique_ptr<dsp::Block> make(std::uint32_t rate, const std::string *state)
    {
        std::unique_ptr<dsp::Block> made;
        if (options.format == "lv2") {
            if (!lv2) {
                lv2 = std::make_shared<pluginhost::Lv2World>();
            }
            made = pluginhost::Lv2Plugin::make(lv2,
                                               options.plugin,
                                               options.mono == "downmix" ? pluginhost::Lv2Plugin::Mono::Downmix
                                                                         : pluginhost::Lv2Plugin::Mono::DualMono);
        } else if (options.format == "ladspa") {
            // A LADSPA plugin's state is its control values, which go in
            // as any block's do, below.
            made = pluginhost::LadspaPlugin::make(options.plugin,
                                                  options.mono == "downmix" ? pluginhost::LadspaPlugin::Mono::Downmix
                                                                            : pluginhost::LadspaPlugin::Mono::DualMono);
#ifdef RB_HAVE_CLAP
        } else if (options.format == "clap") {
            if (!clap) {
                clap = pluginhost::ClapLibrary::open(options.location);
            }
            if (clap) {
                made = pluginhost::ClapPlugin::make(clap,
                                                    options.plugin,
                                                    options.mono == "downmix" ? pluginhost::ClapPlugin::Mono::Downmix
                                                                              : pluginhost::ClapPlugin::Mono::DualMono);
            }
#endif
        } else {
            made = plugin::makeBuiltinPlugin(options.plugin, options.latency);
        }
        if (!made) {
            return nullptr;
        }
        auto *lv2Plugin = dynamic_cast<pluginhost::Lv2Plugin *>(made.get());
#ifdef RB_HAVE_CLAP
        auto *clapPlugin = dynamic_cast<pluginhost::ClapPlugin *>(made.get());
#else
        const void *clapPlugin = nullptr;
#endif
        if (state != nullptr && lv2Plugin == nullptr && clapPlugin == nullptr) {
            (void)made->loadState(parseState(*made, *state));
        }
        dsp::PrepareSpec spec;
        spec.sampleRate = rate;
        spec.inputChannels = made->info().inputs.maxCount;
        spec.outputChannels = made->info().outputs.maxCount;
        if (!made->prepare(spec)) {
            return nullptr;
        }
        // An LV2 state goes into the plugin once it is made, which prepare
        // does.
        if (state != nullptr && lv2Plugin != nullptr && !state->empty() && !lv2Plugin->loadStateText(*state)) {
            return nullptr;
        }
#ifdef RB_HAVE_CLAP
        // So does a CLAP one: CLAP's state is the plugin's own bytes.
        if (state != nullptr && clapPlugin != nullptr && !state->empty() && !clapPlugin->loadStateText(*state)) {
            return nullptr;
        }
#endif
        return made;
    }

    struct Swap {
        plugin::HostProcessor *processor;
        dsp::Block *next;
        std::uint32_t rate;
        std::size_t paramCount;
        dsp::Block *previous;
    };

    /// Hands \a next to the data thread between two cycles, and frees what it
    /// was running once it has let go of it.
    void install(std::unique_ptr<dsp::Block> next, std::uint32_t rate)
    {
        Swap swap{&processor, next.get(), rate, next->params().size(), nullptr};
        const auto run = [](spa_loop *, bool, std::uint32_t, const void *, std::size_t, void *data) -> int {
            auto *swap = static_cast<Swap *>(data);
            swap->previous = swap->processor->exchange(swap->next, swap->rate, swap->paramCount);
            return 0;
        };
        if (filter != nullptr) {
            pw_loop_invoke(pw_filter_get_data_loop(filter), run, 0, nullptr, 0, true, &swap);
        } else {
            swap.previous = processor.exchange(swap.next, swap.rate, swap.paramCount);
        }
        block = std::move(next); // the one the data thread gave back goes with the old owner
        shared.block().latencySamples.store(block->info().latencySamples, std::memory_order_relaxed);
    }

    [[nodiscard]] std::string stateText() const
    {
        // **LV2's own**, as Turtle: the controls, and whatever the plugin
        // keeps through the state extension.
        if (const auto *lv2Plugin = dynamic_cast<const pluginhost::Lv2Plugin *>(block.get()); lv2Plugin != nullptr) {
            return lv2Plugin->stateText();
        }
#ifdef RB_HAVE_CLAP
        // **CLAP's own**: the plugin's bytes, as base64.
        if (const auto *clapPlugin = dynamic_cast<const pluginhost::ClapPlugin *>(block.get()); clapPlugin != nullptr) {
            return clapPlugin->stateText();
        }
#endif
        std::string text;
        const dsp::ParamValueMap state = block->saveState();
        for (const dsp::ParamDescriptor &descriptor : block->params()) {
            const auto found = state.find(descriptor.id);
            if (found == state.end()) {
                continue;
            }
            const dsp::ParamValue &value = found->second;
            text += descriptor.id + "=";
            switch (descriptor.type) {
            case dsp::ParamType::Float:
            case dsp::ParamType::Int:
                text += std::to_string(value.asNumber().value_or(0.0));
                break;
            case dsp::ParamType::Bool:
                text += value.asBool().value_or(false) ? "true" : "false";
                break;
            default:
                text += value.text();
                break;
            }
            text += "\n";
        }
        return text;
    }

    [[nodiscard]] static dsp::ParamValueMap parseState(const dsp::Block &target, const std::string &text)
    {
        dsp::ParamValueMap state;
        std::size_t at = 0;
        while (at < text.size()) {
            const std::size_t end = std::min(text.find('\n', at), text.size());
            const std::string line = text.substr(at, end - at);
            at = end + 1;
            const std::size_t equals = line.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            const std::string id = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);
            for (const dsp::ParamDescriptor &descriptor : target.params()) {
                if (descriptor.id != id) {
                    continue;
                }
                switch (descriptor.type) {
                case dsp::ParamType::Float:
                    state.emplace(id, dsp::ParamValue::ofFloat(std::strtof(value.c_str(), nullptr)));
                    break;
                case dsp::ParamType::Int:
                    state.emplace(id, dsp::ParamValue::ofInt(std::strtoll(value.c_str(), nullptr, 10)));
                    break;
                case dsp::ParamType::Bool:
                    state.emplace(id, dsp::ParamValue::ofBool(value == "true"));
                    break;
                case dsp::ParamType::Enum:
                    state.emplace(id, dsp::ParamValue::ofEnum(value));
                    break;
                default:
                    state.emplace(id, dsp::ParamValue::ofString(value));
                    break;
                }
            }
        }
        return state;
    }

    void onMessage(const plugin::Message &message)
    {
        switch (message.type) {
        case plugin::MessageType::Quit:
            pw_main_loop_quit(loop);
            break;
        case plugin::MessageType::GetState:
            send(plugin::MessageType::State, stateText());
            break;
        case plugin::MessageType::SetState: {
            // **A fresh plugin with the state in it, swapped in whole**: the
            // data thread never sees a plugin half-way through loading.
            std::unique_ptr<dsp::Block> next = make(shared.block().preparedRate.load(), &message.payload);
            const bool loaded = next != nullptr;
            if (loaded) {
                install(std::move(next), shared.block().preparedRate.load());
            }
            // **Said once the new plugin is the one running**: the engine
            // holds back the changes made meanwhile, because through the
            // ring they would reach the plugin being replaced and be lost.
            send(plugin::MessageType::StateLoaded, loaded ? "ok" : "refused");
            break;
        }
        default:
            break;
        }
    }
};

void onProcess(void *data, spa_io_position *position)
{
    auto *host = static_cast<Host *>(data);
    const std::uint32_t frames = position != nullptr ? static_cast<std::uint32_t>(position->clock.duration) : 0;
    const std::uint32_t rate = position != nullptr ? position->clock.rate.denom : 0;
    for (std::size_t index = 0; index < host->inputs.size(); ++index) {
        host->inputBuffers[index] = static_cast<float *>(pw_filter_get_dsp_buffer(host->inputs[index], frames));
    }
    for (std::size_t index = 0; index < host->outputs.size(); ++index) {
        host->outputBuffers[index] = stillUnread(host->outputs[index])
            ? nullptr
            : static_cast<float *>(pw_filter_get_dsp_buffer(host->outputs[index], frames));
    }
    host->processor.process(host->inputBuffers, host->outputBuffers, frames, rate);
}

void onStateChanged(void *data, pw_filter_state, pw_filter_state state, const char *)
{
    auto *host = static_cast<Host *>(data);
    if ((state == PW_FILTER_STATE_PAUSED || state == PW_FILTER_STATE_STREAMING) && !host->announced) {
        host->announced = true;
        host->send(plugin::MessageType::Ready,
                   host->nodeName + "\n" + std::to_string(host->block->info().latencySamples));
    } else if (state == PW_FILTER_STATE_ERROR || state == PW_FILTER_STATE_UNCONNECTED) {
        pw_main_loop_quit(host->loop);
    }
}

[[nodiscard]] pw_filter_events filterEvents()
{
    pw_filter_events events{};
    events.version = PW_VERSION_FILTER_EVENTS;
    events.state_changed = onStateChanged;
    events.process = onProcess;
    events.io_changed = [](void *, void *port, std::uint32_t id, void *area, std::uint32_t size) {
        if (port != nullptr && id == SPA_IO_Buffers) {
            static_cast<Port *>(port)->io.store(
                area != nullptr && size >= sizeof(spa_io_buffers) ? static_cast<spa_io_buffers *>(area) : nullptr,
                std::memory_order_release);
        }
    };
    return events;
}

const pw_filter_events g_events = filterEvents();

void onControl(void *data, int, std::uint32_t mask)
{
    auto *host = static_cast<Host *>(data);
    if ((mask & (SPA_IO_ERR | SPA_IO_HUP)) != 0) {
        // The engine has gone: so has the reason to be here.
        pw_main_loop_quit(host->loop);
        return;
    }
    for (;;) {
        const std::optional<plugin::Message> message = plugin::receiveMessage(host->options.controlFd, false);
        if (!message) {
            if (!plugin::wouldBlock()) {
                pw_main_loop_quit(host->loop);
            }
            return;
        }
        host->onMessage(*message);
    }
}

/// Every so often: has PipeWire moved the graph to a rate the plugin is not
/// prepared for? Then a plugin prepared for it, state and all.
void onRateCheck(void *data, std::uint64_t)
{
    auto *host = static_cast<Host *>(data);
    // **The latency the plugin reports now**, told to the engine when it
    // changes: an LV2 plugin or a LADSPA one says it through a
    // port, as it runs.
    bool reports = true;
    if (auto *lv2Plugin = dynamic_cast<pluginhost::Lv2Plugin *>(host->block.get()); lv2Plugin != nullptr) {
        lv2Plugin->refreshLatency();
    } else if (auto *ladspaPlugin = dynamic_cast<pluginhost::LadspaPlugin *>(host->block.get());
               ladspaPlugin != nullptr) {
        ladspaPlugin->refreshLatency();
    } else {
        reports = false;
    }
    if (reports) {
        const std::uint32_t latency = host->block->info().latencySamples;
        if (latency != host->reportedLatency) {
            host->reportedLatency = latency;
            host->shared.block().latencySamples.store(latency, std::memory_order_relaxed);
            host->send(plugin::MessageType::Latency, std::to_string(latency));
        }
    }
#ifdef RB_HAVE_CLAP
    // **A CLAP plugin's turn on the main thread**: the calls it asked
    // for, and the latency it reports, which changes only when it has been
    // restarted — which it asks for, and which is a plugin made again with
    // its state, as for a new rate.
    if (auto *clapPlugin = dynamic_cast<pluginhost::ClapPlugin *>(host->block.get()); clapPlugin != nullptr) {
        if (clapPlugin->idle()) {
            const std::string state = host->stateText();
            const std::uint32_t rate = host->shared.block().preparedRate.load();
            if (std::unique_ptr<dsp::Block> next = host->make(rate, &state)) {
                host->install(std::move(next), rate);
            }
        }
        const std::uint32_t latency = host->block->info().latencySamples;
        if (latency != host->reportedLatency) {
            host->reportedLatency = latency;
            host->shared.block().latencySamples.store(latency, std::memory_order_relaxed);
            host->send(plugin::MessageType::Latency, std::to_string(latency));
        }
    }
#endif
    const std::uint32_t wanted = host->shared.block().rateWanted.load(std::memory_order_relaxed);
    if (wanted == 0) {
        return;
    }
    const std::string state = host->stateText();
    if (std::unique_ptr<dsp::Block> next = host->make(wanted, &state)) {
        host->install(std::move(next), wanted);
    }
}

} // namespace

int main(int argc, char **argv)
{
    // **Which build this is**, before anything else looks at the
    // arguments.
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        const std::string_view version = rb::versionFull();
        std::printf("reverblabr-plugin-host %.*s\n", static_cast<int>(version.size()), version.data());
        return 0;
    }
    // **Describing, not running**: one candidate, its plugins to
    // standard output, and out.
    if (argc == 4 && std::string_view{argv[1]} == "--scan") {
        return rb::pluginhost::scan(argv[2], argv[3]);
    }
    const std::optional<Options> options = parse(argc, argv);
    if (!options) {
        std::fprintf(stderr,
                     "usage: reverblabr-plugin-host --format builtin|lv2|ladspa|clap --plugin <id> --instance <id> "
                     "--control-fd <fd> --shm-fd <fd> [--latency <samples>] [--mono dualMono|downmix] "
                     "[--location <path>]\n");
        return 2;
    }
    std::expected<plugin::SharedMemory, plugin::SharedError> shared = plugin::SharedMemory::map(options->sharedFd);
    if (!shared) {
        (void)plugin::sendMessage(options->controlFd, {plugin::MessageType::Error, "WrongSharedMemory"});
        return 3;
    }
    Host host(*options, std::move(*shared));
    const bool known = options->format == "builtin" || options->format == "lv2" || options->format == "ladspa"
#ifdef RB_HAVE_CLAP
        || options->format == "clap"
#endif
        ;
    if (!known) {
        host.send(plugin::MessageType::Error, "UnsupportedFormat");
        return 3;
    }
    constexpr std::uint32_t kStartingRate = 48000;
    std::unique_ptr<dsp::Block> first = host.make(kStartingRate, nullptr);
    if (!first) {
        host.send(plugin::MessageType::Error, "UnknownPlugin");
        return 3;
    }
    host.install(std::move(first), kStartingRate);

    pw_init(&argc, &argv);
    host.loop = pw_main_loop_new(nullptr);
    pw_loop *loop = pw_main_loop_get_loop(host.loop);
    host.nodeName = "reverblabr.plugin." + options->instance;
    pw_properties *props = pw_properties_new(PW_KEY_NODE_NAME,
                                             host.nodeName.c_str(),
                                             PW_KEY_NODE_DESCRIPTION,
                                             ("Reverblabr plugin " + options->plugin).c_str(),
                                             PW_KEY_MEDIA_TYPE,
                                             "Audio",
                                             PW_KEY_MEDIA_CATEGORY,
                                             "Filter",
                                             PW_KEY_APP_NAME,
                                             "Reverblabr",
                                             PW_KEY_NODE_AUTOCONNECT,
                                             "false",
                                             "node.always-process",
                                             "true",
                                             "reverblabr.plugin.instance",
                                             options->instance.c_str(),
                                             nullptr);
    host.filter = pw_filter_new_simple(loop, host.nodeName.c_str(), props, &g_events, &host);
    const auto addPorts = [&host](pw_direction direction, const dsp::PortSpec &spec, std::vector<Port *> &ports) {
        for (std::uint32_t index = 0; index < spec.maxCount; ++index) {
            const std::string name{spec.label(index)};
            auto *port = static_cast<Port *>(pw_filter_add_port(
                host.filter,
                direction,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                sizeof(Port),
                pw_properties_new(
                    PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, name.c_str(), nullptr),
                nullptr,
                0));
            if (port != nullptr) {
                // The filter hands over zeroed memory of that size.
                new (port) Port{};
            }
            ports.push_back(port);
        }
    };
    addPorts(PW_DIRECTION_INPUT, host.block->info().inputs, host.inputs);
    addPorts(PW_DIRECTION_OUTPUT, host.block->info().outputs, host.outputs);
    host.inputBuffers.assign(host.inputs.size(), nullptr);
    host.outputBuffers.assign(host.outputs.size(), nullptr);

    pw_loop_add_io(loop, options->controlFd, SPA_IO_IN | SPA_IO_ERR | SPA_IO_HUP, false, onControl, &host);
    spa_source *rateCheck = pw_loop_add_timer(loop, onRateCheck, &host);
    timespec period{0, 50'000'000};
    pw_loop_update_timer(loop, rateCheck, &period, &period, false);

    if (pw_filter_connect(host.filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) < 0) {
        host.send(plugin::MessageType::Error, "NodeRefused");
        return 4;
    }
    pw_main_loop_run(host.loop);

    pw_filter_destroy(host.filter);
    pw_main_loop_destroy(host.loop);
    pw_deinit();
    return 0;
}
