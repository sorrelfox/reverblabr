// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// reverblabr-model-host (ADR-0031): one model in a process of its own, beside
// the session. The block starts it, hands it a block of shared memory and a
// socket, and never waits for it; a model that crashes takes this process
// with it and nothing else.
//
//   reverblabr-model-host --inspect <folder>
//   reverblabr-model-host --run --folder <folder> --rate <hz> --latency <frames>
//                         --mono dualMono|downmix --control-fd <fd> --shm-fd <fd>

#include "buildversion.h"
#include "model/recognise.h"
#include "model/shared.h"
#include "onnxrunner.h"
#include "pipeline.h"
#include "voiceconverter.h"

#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <charconv>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace rb;

namespace
{

/// What the host exits with when its reason is in the memory.
constexpr int kExitRefused = 3;
/// When the arguments or the memory are not what they should be.
constexpr int kExitUsage = 2;

/// The most address space a host may hold: a bound on what a file can make
/// it ask for. Far past the 2 GiB a model can be, as a runtime's arenas and
/// its threads' stacks count as well.
constexpr rlim_t kMostAddressSpace = 32ULL << 30;

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define RB_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define RB_SANITIZED 1
#endif
#endif

struct Options {
    std::string folder;
    std::uint32_t rate = 0;
    std::uint32_t latency = 0;
    bool downmix = false;
    int controlFd = -1;
    int sharedFd = -1;
};

[[nodiscard]] std::optional<Options> parse(int argc, char **argv)
{
    Options options;
    for (int index = 2; index + 1 < argc; index += 2) {
        const std::string_view key = argv[index];
        const std::string_view value = argv[index + 1];
        const auto number = [&value](auto &into) {
            return std::from_chars(value.data(), value.data() + value.size(), into).ec == std::errc{};
        };
        if (key == "--folder") {
            options.folder = model::localFolder(std::string{value});
        } else if (key == "--rate") {
            if (!number(options.rate)) {
                return std::nullopt;
            }
        } else if (key == "--latency") {
            if (!number(options.latency)) {
                return std::nullopt;
            }
        } else if (key == "--mono") {
            options.downmix = value == "downmix";
        } else if (key == "--control-fd") {
            if (!number(options.controlFd)) {
                return std::nullopt;
            }
        } else if (key == "--shm-fd") {
            if (!number(options.sharedFd)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    if (options.folder.empty() || options.rate == 0 || options.controlFd < 0 || options.sharedFd < 0) {
        return std::nullopt;
    }
    return options;
}

/// **The walls a process can put up by itself**: no new rights, nobody
/// reading its memory, no core file with a model in it, and a bound on the
/// memory a file can make it ask for.
void limitThisProcess()
{
    ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    ::prctl(PR_SET_DUMPABLE, 0);
    const rlimit noCore{0, 0};
    ::setrlimit(RLIMIT_CORE, &noCore);
#ifndef RB_SANITIZED
    // A sanitizer's own maps are far larger than any limit worth setting.
    const rlimit space{kMostAddressSpace, kMostAddressSpace};
    ::setrlimit(RLIMIT_AS, &space);
#endif
}

[[noreturn]] void refuse(model::SharedRings &rings, model::Refusal why)
{
    rings.header().reason.store(static_cast<std::uint32_t>(why), std::memory_order_relaxed);
    rings.header().phase.store(static_cast<std::uint32_t>(model::Phase::Failed), std::memory_order_release);
    ::_exit(kExitRefused);
}

int inspect(const char *folder)
{
    limitThisProcess();
    const std::string text = model::describeAsText(model::recognise(folder));
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
}

int run(const Options &options)
{
    limitThisProcess();
    std::expected<model::SharedRings, model::SharedError> mapped = model::SharedRings::map(options.sharedFd);
    if (!mapped) {
        return kExitUsage;
    }
    model::SharedRings rings = std::move(*mapped);

    // **The socket closing is the order to go**, at any moment, the middle
    // of a run included: nothing here needs putting away.
    std::thread([control = options.controlFd] {
        char byte = 0;
        while (::read(control, &byte, 1) > 0) { }
        ::_exit(0);
    }).detach();

    rings.header().phase.store(static_cast<std::uint32_t>(model::Phase::Loading), std::memory_order_release);
    // Read again: the folder may have changed since it was inspected, and
    // only what this process has read itself is believed here.
    const model::Description description = model::recognise(options.folder);
    if (!description.usable()) {
        refuse(rings, description.refusal);
    }
    const std::optional<model::Timing> timing = model::timingFor(description, options.rate, 0);
    if (!timing || timing->hopFrames + timing->pathDelay > options.latency) {
        refuse(rings, model::Refusal::RateNotUsable);
    }
    // **A voice is one voice**: a person speaks once, and a run costs too
    // much to make twice for two sides that hold the same thing.
    const bool voice = description.family == model::Family::VoiceConversion;
    const bool downmix = options.downmix || voice;
    const std::size_t streams = description.channels == 2 || downmix ? 1 : model::kRingChannels;
    std::unique_ptr<modelhost::ModelRunner> runner;
    if (voice) {
        runner = std::make_unique<modelhost::VoiceConverter>(options.folder, description);
    } else {
        runner = std::make_unique<modelhost::OnnxRunner>(options.folder, description, streams);
    }
    if (runner->refusal() != model::Refusal::None) {
        refuse(rings, runner->refusal());
    }
    rings.header().device.store(static_cast<std::uint32_t>(runner->device()), std::memory_order_relaxed);

    // **A run on silence first**: the shapes it gives back are checked
    // before any sound depends on them, and a runtime's lazy allocations are
    // out of the way.
    std::vector<float> silence(std::size_t{description.channels} * description.frameSize, 0.0f);
    std::vector<float> answer(std::size_t{description.channels} * description.hopOut(), 0.0f);
    try {
        for (std::size_t stream = 0; stream < streams; ++stream) {
            if (!runner->run(stream, silence, answer)) {
                refuse(rings, model::Refusal::WrongShape);
            }
        }
    } catch (const std::exception &) {
        refuse(rings, model::Refusal::RuntimeRefused);
    }
    runner->clearState();

    modelhost::PipelineOptions pipeline;
    pipeline.sessionRate = options.rate;
    pipeline.latency = options.latency;
    pipeline.downmix = downmix;
    return modelhost::runPipeline(rings, description, pipeline, *runner);
}

} // namespace

int main(int argc, char **argv)
{
    const std::string_view mode = argc > 1 ? std::string_view{argv[1]} : std::string_view{};
    if (argc == 2 && mode == "--version") {
        const std::string_view version = rb::versionFull();
        std::printf("reverblabr-model-host %.*s\n", static_cast<int>(version.size()), version.data());
        return 0;
    }
    if (argc == 3 && mode == "--inspect") {
        return inspect(argv[2]);
    }
    const std::optional<Options> options = mode == "--run" ? parse(argc, argv) : std::nullopt;
    if (!options) {
        std::fprintf(stderr,
                     "usage: reverblabr-model-host --inspect <folder>\n"
                     "       reverblabr-model-host --run --folder <folder> --rate <hz> --latency <frames> "
                     "--mono dualMono|downmix --control-fd <fd> --shm-fd <fd>\n");
        return kExitUsage;
    }
    try {
        return run(*options);
    } catch (const std::exception &) {
        // A run that threw: told as a crash, which is what it is.
        return 4;
    }
}
