// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model/recognise.h"

#include "model/minijson.h"
#include "model/onnxreader.h"
#include "model/voice.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <thread>
#include <vector>

namespace rb::model
{

namespace
{

/// How many names of a folder are looked at, and how many ONNX files.
constexpr std::size_t kMostEntries = 4096;
constexpr std::size_t kMostOnnxFiles = 16;

constexpr std::uint32_t kLowestRate = 8000;
constexpr std::uint32_t kHighestRate = 192000;
constexpr std::uint32_t kShortestFrame = 16;
/// Five seconds at the highest rate.
constexpr std::uint32_t kLongestFrame = 960000;
constexpr double kLongestFrameSeconds = 5.0;
constexpr double kLongestLookAheadSeconds = 10.0;
constexpr std::size_t kMostStates = 64;
constexpr std::size_t kMostConstants = 16;
constexpr std::size_t kLongestName = 256;
constexpr std::uint32_t kMostThreads = 8;

/// How late the streaming DeepFilterNet answers: three hops of its 480,
/// measured on an export through this block.
constexpr std::uint32_t kDeepFilterLookAhead = 1440;

// **How a voice is cut into runs.** Each run takes a window of sound, and the
// voice's answer to the hop at its end is what is kept.

/// What a new run has to wait for: less is a shorter delay, and more is less
/// of a core, as a run costs the same however little it adds.
constexpr std::uint32_t kVoiceHop = 8 * kVoiceContentFrame;
/// What the content and pitch models hear of the past.
constexpr std::uint32_t kVoiceWindow = 50 * kVoiceContentFrame;
/// What the voice itself is given: the costliest of the three by far, and it
/// needs less of the past than they do.
constexpr std::uint32_t kVoiceFrames = 40;
/// Frames left out at the end: they have nothing after them.
constexpr std::uint32_t kVoiceTailFrames = 2;
/// How long one run fades into the next.
constexpr double kVoiceCrossfadeSeconds = 0.02;
/// A run's share of the latency, past what the spare time holds.
constexpr std::uint32_t kVoiceRunMs = 120;
/// Threads a graph may use, and how many a machine keeps for everything else.
constexpr std::uint32_t kVoiceThreads = 4;
constexpr std::uint32_t kLowestVoiceRate = 8000;
constexpr std::uint32_t kHighestVoiceRate = 48000;
/// How many frames a second the voice makes.
constexpr std::uint32_t kVoiceFramesPerSecond = kVoiceInputRate / kVoiceFrame;
/// The sizes a content encoder's features come in.
constexpr std::array<std::int64_t, 2> kContentSizes{256, 768};

constexpr std::int32_t kElementFloat = 1;
constexpr std::int32_t kElementInt64 = 7;

[[nodiscard]] std::string lower(std::string_view text)
{
    std::string out{text};
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] bool endsWith(std::string_view text, std::string_view ending)
{
    return text.ends_with(ending);
}

/// A name fit to be shown and handed on: one line of plain characters.
[[nodiscard]] bool plainName(std::string_view name)
{
    return !name.empty() && name.size() <= kLongestName
        && std::ranges::none_of(name, [](unsigned char c) { return c < 0x20 || c == 0x7F; });
}

/// A file directly in the folder, by a name that came from a file.
[[nodiscard]] bool bareFileName(std::string_view name)
{
    return plainName(name) && name != "." && name != ".." && name.find('/') == std::string_view::npos;
}

[[nodiscard]] std::vector<std::string> namesIn(const std::string &folder)
{
    std::vector<std::string> names;
    DIR *directory = ::opendir(folder.c_str());
    if (directory == nullptr) {
        return names;
    }
    while (names.size() < kMostEntries) {
        const dirent *entry = ::readdir(directory);
        if (entry == nullptr) {
            break;
        }
        const std::string_view name{entry->d_name};
        if (bareFileName(name)) {
            names.emplace_back(name);
        }
    }
    ::closedir(directory);
    std::ranges::sort(names);
    return names;
}

[[nodiscard]] bool isFile(const std::string &path)
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

/// The configuration's bytes, or nothing when it is too large to be one.
[[nodiscard]] std::optional<std::string> readSmallFile(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::nullopt;
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || static_cast<std::size_t>(info.st_size) > kMostJsonBytes) {
        ::close(fd);
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(info.st_size), '\0');
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t got = ::read(fd, text.data() + done, text.size() - done);
        if (got <= 0) {
            break;
        }
        done += static_cast<std::size_t>(got);
    }
    ::close(fd);
    return done == text.size() ? std::optional{std::move(text)} : std::nullopt;
}

[[nodiscard]] Refusal refusalOf(OnnxFault fault)
{
    switch (fault) {
    case OnnxFault::TooLarge:
        return Refusal::FileTooLarge;
    case OnnxFault::UnsafeReference:
        return Refusal::UnsafeReference;
    case OnnxFault::Unreadable:
    case OnnxFault::NotOnnx:
        break;
    }
    return Refusal::NotAModel;
}

[[nodiscard]] std::optional<std::uint32_t> wholeNumber(const JsonValue *value)
{
    if (value == nullptr || value->kind != JsonValue::Kind::Number || value->number < 0.0
        || value->number > 4294967295.0 || std::floor(value->number) != value->number) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value->number);
}

[[nodiscard]] std::optional<std::string> textOf(const JsonValue *value)
{
    if (value == nullptr || value->kind != JsonValue::Kind::String || !plainName(value->text)) {
        return std::nullopt;
    }
    return value->text;
}

/// Whether \a document is the configuration of a voice conversion folder.
[[nodiscard]] bool isVoiceConfiguration(const JsonValue &document)
{
    const std::optional<std::string> family = textOf(document.find("family"));
    return family == "voiceConversion";
}

/// The three files of \a document, and what it says of them. Nothing but
/// the names yet: what the files are is read from them afterwards.
[[nodiscard]] std::optional<Description> parseVoiceConfiguration(const JsonValue &document)
{
    const std::optional<std::uint32_t> format = wholeNumber(document.find("format"));
    const std::optional<std::string> file = textOf(document.find("file"));
    const std::optional<std::string> content = textOf(document.find("contentModel"));
    const std::optional<std::string> pitch = textOf(document.find("pitchModel"));
    if (format != std::uint32_t{1} || !file || !content || !pitch || !bareFileName(*file) || !bareFileName(*content)
        || !bareFileName(*pitch)) {
        return std::nullopt;
    }
    Description description;
    description.family = Family::VoiceConversion;
    description.modelFile = *file;
    description.voice.contentFile = *content;
    description.voice.pitchFile = *pitch;
    description.threads = 0;
    if (const JsonValue *rate = document.find("sampleRate"); rate != nullptr) {
        const std::optional<std::uint32_t> value = wholeNumber(rate);
        if (!value || *value < kLowestVoiceRate || *value > kHighestVoiceRate || *value % kVoiceFramesPerSecond != 0) {
            return std::nullopt;
        }
        description.sampleRate = *value;
    }
    if (const JsonValue *threads = document.find("threads"); threads != nullptr) {
        const std::optional<std::uint32_t> count = wholeNumber(threads);
        if (!count || *count == 0 || *count > kMostThreads) {
            return std::nullopt;
        }
        description.threads = *count;
    }
    if (const JsonValue *licence = document.find("licence"); licence != nullptr) {
        const std::optional<std::string> name = textOf(licence);
        if (!name || !bareFileName(*name)) {
            return std::nullopt;
        }
        description.licenceFile = *name;
    }
    return description;
}

/// The configuration in \a text, in bounds. Names are checked against the
/// model afterwards.
[[nodiscard]] std::optional<Description> parseConfiguration(std::string_view text)
{
    const std::optional<JsonValue> document = parseJson(text);
    if (!document || document->kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    if (isVoiceConfiguration(*document)) {
        return parseVoiceConfiguration(*document);
    }
    const std::optional<std::uint32_t> format = wholeNumber(document->find("format"));
    const std::optional<std::string> file = textOf(document->find("file"));
    const std::optional<std::uint32_t> rate = wholeNumber(document->find("sampleRate"));
    const std::optional<std::uint32_t> channels = wholeNumber(document->find("channels"));
    const std::optional<std::uint32_t> frame = wholeNumber(document->find("frameSize"));
    const std::optional<std::string> input = textOf(document->find("input"));
    const std::optional<std::string> output = textOf(document->find("output"));
    if (format != std::uint32_t{1} || !file || !rate || !channels || !frame || !input || !output
        || !bareFileName(*file)) {
        return std::nullopt;
    }
    Description description;
    description.family = Family::Configured;
    description.modelFile = *file;
    description.sampleRate = *rate;
    description.channels = *channels;
    description.frameSize = *frame;
    description.hopSize = *frame;
    description.input = *input;
    description.output = *output;
    if (const JsonValue *hop = document->find("hopSize"); hop != nullptr) {
        const std::optional<std::uint32_t> size = wholeNumber(hop);
        if (!size) {
            return std::nullopt;
        }
        description.hopSize = *size;
    }
    if (const JsonValue *latency = document->find("latencySamples"); latency != nullptr) {
        const std::optional<std::uint32_t> size = wholeNumber(latency);
        if (!size) {
            return std::nullopt;
        }
        description.latencySamples = *size;
    }
    if (const JsonValue *threads = document->find("threads"); threads != nullptr) {
        const std::optional<std::uint32_t> count = wholeNumber(threads);
        if (!count || *count == 0 || *count > kMostThreads) {
            return std::nullopt;
        }
        description.threads = *count;
    }
    if (const JsonValue *licence = document->find("licence"); licence != nullptr) {
        const std::optional<std::string> name = textOf(licence);
        if (!name || !bareFileName(*name)) {
            return std::nullopt;
        }
        description.licenceFile = *name;
    }
    if (const JsonValue *state = document->find("state"); state != nullptr) {
        if (state->kind != JsonValue::Kind::Array || state->items.size() > kMostStates) {
            return std::nullopt;
        }
        for (const JsonValue &pair : state->items) {
            const std::optional<std::string> in = textOf(pair.find("input"));
            const std::optional<std::string> out = textOf(pair.find("output"));
            if (!in || !out) {
                return std::nullopt;
            }
            description.state.push_back(StatePair{*in, *out});
        }
    }
    if (const JsonValue *constants = document->find("constants"); constants != nullptr) {
        if (constants->kind != JsonValue::Kind::Object || constants->members.size() > kMostConstants) {
            return std::nullopt;
        }
        for (const auto &[name, value] : constants->members) {
            if (!plainName(name) || value.kind != JsonValue::Kind::Number) {
                return std::nullopt;
            }
            description.constants.push_back(Constant{name, static_cast<float>(value.number)});
        }
    }
    const double rateNow = *rate;
    if (*rate < kLowestRate || *rate > kHighestRate || *channels < 1 || *channels > 2 || *frame < kShortestFrame
        || *frame > kLongestFrame || *frame / rateNow > kLongestFrameSeconds || description.hopSize < kShortestFrame
        || description.hopSize > *frame || description.latencySamples / rateNow > kLongestLookAheadSeconds) {
        return std::nullopt;
    }
    return description;
}

[[nodiscard]] bool hasPort(const std::vector<OnnxPort> &ports, std::string_view name)
{
    return std::ranges::any_of(ports, [name](const OnnxPort &port) { return port.name == name; });
}

/// Whether the configuration \a description names what \a summary has, and
/// leaves no input unfilled.
[[nodiscard]] bool matches(const Description &description, const OnnxSummary &summary)
{
    if (!hasPort(summary.inputs, description.input) || !hasPort(summary.outputs, description.output)) {
        return false;
    }
    std::set<std::string_view> filled{description.input};
    for (const StatePair &pair : description.state) {
        if (!hasPort(summary.inputs, pair.input) || !hasPort(summary.outputs, pair.output)) {
            return false;
        }
        filled.insert(pair.input);
    }
    for (const Constant &constant : description.constants) {
        if (!hasPort(summary.inputs, constant.input)) {
            return false;
        }
        filled.insert(constant.input);
    }
    return std::ranges::all_of(summary.inputs, [&filled](const OnnxPort &port) { return filled.contains(port.name); });
}

/// The family an ONNX graph's inputs show, or Unknown.
[[nodiscard]] Family familyOf(const OnnxSummary &summary)
{
    const auto has = [&summary](std::string_view name) { return hasPort(summary.inputs, name); };
    if (has("input_frame") && has("states") && has("atten_lim_db")) {
        return Family::DeepFilterNetStream;
    }
    if (has("mix") && has("conv_cache") && has("tra_cache") && has("inter_cache")) {
        return Family::Cleanup;
    }
    // Sound comes back from features and a pitch. The exports differ in what
    // they call the features, and a voice trained without a pitch has none.
    if ((has("phone") || has("feats")) && has("pitch") && has("pitchf")) {
        return Family::VoiceConversion;
    }
    if (has("phone") && has("phone_lengths")) {
        return Family::VoiceConversion;
    }
    if (has("input") && has("input_lengths") && has("scales")) {
        return Family::Speech;
    }
    return Family::Unknown;
}

void describeDeepFilter(Description &description, const std::string &file)
{
    description.family = Family::DeepFilterNetStream;
    description.modelFile = file;
    description.sampleRate = 48000;
    description.channels = 1;
    description.frameSize = 480;
    description.hopSize = 480;
    description.latencySamples = kDeepFilterLookAhead;
    description.input = "input_frame";
    description.output = "enhanced_audio_frame";
    description.state = {StatePair{"states", "new_states"}};
    // The limit on how far the sound is turned down: as far as it goes.
    description.constants = {Constant{"atten_lim_db", 100.0f}};
}

[[nodiscard]] Description refused(Refusal refusal, Family family = Family::Unknown)
{
    Description description;
    description.refusal = refusal;
    description.family = family;
    return description;
}

/// A graph of a voice conversion folder.
struct VoiceGraph {
    std::string file;
    OnnxSummary summary;
};

[[nodiscard]] const OnnxPort *portNamed(const std::vector<OnnxPort> &ports,
                                        std::initializer_list<std::string_view> names)
{
    for (const std::string_view name : names) {
        const auto found = std::ranges::find_if(ports, [name](const OnnxPort &port) { return port.name == name; });
        if (found != ports.end()) {
            return &*found;
        }
    }
    return nullptr;
}

/// The output of \a summary that gives features \a bands wide, or nothing.
[[nodiscard]] const OnnxPort *featuresOf(const OnnxSummary &summary, std::int64_t bands)
{
    const auto found = std::ranges::find_if(summary.outputs, [bands](const OnnxPort &port) {
        return port.elementType == kElementFloat && port.dims.size() == 3 && port.dims.back() == bands;
    });
    return found != summary.outputs.end() ? &*found : nullptr;
}

/// The input of a content encoder that takes the sound.
[[nodiscard]] const OnnxPort *soundInputOf(const OnnxSummary &summary)
{
    const auto found = std::ranges::find_if(summary.inputs, [](const OnnxPort &port) {
        return port.elementType == kElementFloat && (port.dims.size() == 2 || port.dims.size() == 3);
    });
    return found != summary.inputs.end() ? &*found : nullptr;
}

/// Whether \a summary is a content encoder: sound in, a size of features
/// such models have out, and nothing else to fill but which samples count.
[[nodiscard]] bool isContentEncoder(const OnnxSummary &summary)
{
    const bool gives = std::ranges::any_of(
        kContentSizes, [&summary](std::int64_t size) { return featuresOf(summary, size) != nullptr; });
    const OnnxPort *const sound = soundInputOf(summary);
    if (!gives || sound == nullptr) {
        return false;
    }
    return std::ranges::all_of(summary.inputs, [sound](const OnnxPort &port) {
        return &port == sound
            || (port.elementType == kElementInt64
                && port.dims.size() == sound->dims.size() - (sound->dims.size() == 3 ? 1 : 0));
    });
}

/// The output of a pitch estimator: three dimensions, the last of them the
/// pitches, or left open, as the exports often leave it.
[[nodiscard]] const OnnxPort *likelihoodsOf(const OnnxSummary &summary)
{
    const auto found = std::ranges::find_if(summary.outputs, [](const OnnxPort &port) {
        return port.elementType == kElementFloat && port.dims.size() == 3
            && (port.dims.back() == std::int64_t{kVoicePitches} || port.dims.back() < 0);
    });
    return found != summary.outputs.end() ? &*found : nullptr;
}

/// Whether \a summary is a pitch estimator: the bands of a spectrum in, and
/// how likely each pitch is out.
[[nodiscard]] bool isPitchEstimator(const OnnxSummary &summary)
{
    return summary.inputs.size() == 1 && summary.inputs[0].elementType == kElementFloat
        && summary.inputs[0].dims.size() == 3 && summary.inputs[0].dims[1] == std::int64_t{kVoiceBands}
    && likelihoodsOf(summary) != nullptr;
}

/// **A voice with what it needs beside it**, or why not. \a chosenRate is the
/// configuration's word for the rate, 0 if it gave none.
[[nodiscard]] Description describeVoice(const VoiceGraph &voice,
                                        const std::vector<VoiceGraph> &contents,
                                        const std::vector<VoiceGraph> &pitches,
                                        std::uint32_t chosenRate,
                                        std::uint32_t chosenThreads)
{
    const auto inputs = [&voice](std::initializer_list<std::string_view> names) {
        return portNamed(voice.summary.inputs, names);
    };
    const OnnxPort *const features = inputs({"phone", "feats"});
    const OnnxPort *const lengths = inputs({"phone_lengths", "p_len"});
    const OnnxPort *const pitch = inputs({"pitch"});
    const OnnxPort *const pitchHz = inputs({"pitchf"});
    const OnnxPort *const speaker = inputs({"ds", "sid"});
    const OnnxPort *const noise = inputs({"rnd"});
    if (features == nullptr || lengths == nullptr || pitch == nullptr || pitchHz == nullptr || speaker == nullptr) {
        return refused(Refusal::NotYetSupported, Family::VoiceConversion);
    }
    const std::set<const OnnxPort *> filled{features, lengths, pitch, pitchHz, speaker, noise};
    if (std::ranges::any_of(voice.summary.inputs,
                            [&filled](const OnnxPort &port) { return !filled.contains(&port); })) {
        return refused(Refusal::NotYetSupported, Family::VoiceConversion);
    }
    if (features->elementType != kElementFloat || features->dims.size() != 3 || features->dims.back() <= 0
        || lengths->elementType != kElementInt64 || pitch->elementType != kElementInt64
        || pitchHz->elementType != kElementFloat || speaker->elementType != kElementInt64
        || (noise != nullptr && noise->elementType != kElementFloat) || voice.summary.outputs.empty()
        || voice.summary.outputs[0].elementType != kElementFloat) {
        return refused(Refusal::WrongShape, Family::VoiceConversion);
    }

    // **A file says nothing of its rate.** A frame is 10 ms, so what the
    // graph makes of one frame says it, and a configuration can say otherwise.
    const std::uint64_t perFrame = chosenRate != 0 ? chosenRate / kVoiceFramesPerSecond : voice.summary.upsampling;
    const std::uint64_t rate = perFrame * kVoiceFramesPerSecond;
    if (rate < kLowestVoiceRate || rate > kHighestVoiceRate) {
        return refused(Refusal::NeedsConfiguration, Family::VoiceConversion);
    }

    if (contents.empty()) {
        return refused(Refusal::NoContentModel, Family::VoiceConversion);
    }
    const std::int64_t bands = features->dims.back();
    const auto content = std::ranges::find_if(
        contents, [bands](const VoiceGraph &graph) { return featuresOf(graph.summary, bands) != nullptr; });
    if (content == contents.end()) {
        return refused(Refusal::ContentDoesNotFit, Family::VoiceConversion);
    }
    if (pitches.empty()) {
        return refused(Refusal::NoPitchModel, Family::VoiceConversion);
    }
    const VoiceGraph &estimator = pitches.front();

    const OnnxPort *const sound = soundInputOf(content->summary);
    Description description;
    description.family = Family::VoiceConversion;
    description.modelFile = voice.file;
    description.output = voice.summary.outputs[0].name;
    description.sampleRate = static_cast<std::uint32_t>(rate);
    description.inputRate = kVoiceInputRate;
    description.channels = 1;
    description.frameSize = kVoiceWindow;
    description.hopSize = kVoiceHop;
    description.outputHop = kVoiceHop / kVoiceFrame * static_cast<std::uint32_t>(perFrame);
    description.runMs = kVoiceRunMs;
    const std::uint32_t machine = std::max(1U, std::thread::hardware_concurrency() / 2);
    description.threads = chosenThreads != 0 ? chosenThreads : std::min(kVoiceThreads, machine);

    VoiceSet &set = description.voice;
    set.contentFile = content->file;
    set.contentInput = sound->name;
    for (const OnnxPort &port : content->summary.inputs) {
        if (port.name != sound->name) {
            set.contentMask = port.name;
        }
    }
    set.contentOutput = featuresOf(content->summary, bands)->name;
    set.contentBands = static_cast<std::uint32_t>(bands);
    set.pitchFile = estimator.file;
    set.pitchInput = estimator.summary.inputs[0].name;
    set.pitchOutput = likelihoodsOf(estimator.summary)->name;
    set.features = features->name;
    set.lengths = lengths->name;
    set.pitch = pitch->name;
    set.pitchHz = pitchHz->name;
    set.speaker = speaker->name;
    set.noise = noise != nullptr ? noise->name : std::string{};
    set.samplesPerFrame = static_cast<std::uint32_t>(perFrame);
    set.voiceFrames = kVoiceFrames;
    set.tailFrames = kVoiceTailFrames;
    set.crossfade = static_cast<std::uint32_t>(std::lround(kVoiceCrossfadeSeconds * static_cast<double>(rate)));

    // **How late a run's sound is**, in samples of what the voice makes: the
    // frames the window does not reach, those left out, and the overlap, of
    // which a run keeps the last for the next one to fade from.
    const std::uint32_t uncovered = kVoiceWindow - voiceFramesIn(kVoiceWindow) * kVoiceFrame;
    description.latencySamples = (uncovered / kVoiceFrame + set.tailFrames) * set.samplesPerFrame + set.crossfade;
    return description;
}

[[nodiscard]] bool hasGgufMagic(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::array<char, 4> magic{};
    const ssize_t got = ::read(fd, magic.data(), magic.size());
    ::close(fd);
    return got == 4 && std::string_view(magic.data(), 4) == "GGUF";
}

[[nodiscard]] std::string licenceFileIn(const std::vector<std::string> &names)
{
    for (const std::string &name : names) {
        const std::string low = lower(name);
        if (low.starts_with("license") || low.starts_with("licence") || low.starts_with("copying")
            || low.starts_with("notice")) {
            return name;
        }
    }
    return {};
}

[[nodiscard]] bool isCodeFile(std::string_view name)
{
    const std::string low = lower(name);
    return endsWith(low, ".pt") || endsWith(low, ".pth") || endsWith(low, ".ckpt") || endsWith(low, ".pkl")
        || endsWith(low, ".pickle");
}

/// A graph the configuration \a named, read from the folder. Nothing when
/// the file is not there, and a refusal in \a fault when it is no model.
[[nodiscard]] std::optional<VoiceGraph> namedGraph(const std::string &folder, const std::string &named, Refusal &fault)
{
    const std::string path = folder + "/" + named;
    if (!endsWith(lower(named), ".onnx") || !isFile(path)) {
        return std::nullopt;
    }
    const std::expected<OnnxSummary, OnnxFault> summary = readOnnxHead(path, folder);
    if (!summary) {
        fault = refusalOf(summary.error());
        return std::nullopt;
    }
    return VoiceGraph{named, *summary};
}

/// What the configuration \a named comes to: the same voice the files would
/// show by themselves, with the choice of files and the rate taken from it.
[[nodiscard]] Description readVoiceConfiguration(const std::string &folder, const Description &named)
{
    Refusal fault = Refusal::None;
    const std::optional<VoiceGraph> voice = namedGraph(folder, named.modelFile, fault);
    const std::optional<VoiceGraph> content = namedGraph(folder, named.voice.contentFile, fault);
    const std::optional<VoiceGraph> pitch = namedGraph(folder, named.voice.pitchFile, fault);
    if (fault != Refusal::None) {
        return refused(fault, Family::VoiceConversion);
    }
    if (!voice) {
        return refused(Refusal::NoVoiceModel, Family::VoiceConversion);
    }
    if (familyOf(voice->summary) != Family::VoiceConversion) {
        return refused(Refusal::WrongShape, Family::VoiceConversion);
    }
    if (content && !isContentEncoder(content->summary)) {
        return refused(Refusal::WrongShape, Family::VoiceConversion);
    }
    if (pitch && !isPitchEstimator(pitch->summary)) {
        return refused(Refusal::WrongShape, Family::VoiceConversion);
    }
    const std::vector<VoiceGraph> contents = content ? std::vector{*content} : std::vector<VoiceGraph>{};
    const std::vector<VoiceGraph> pitches = pitch ? std::vector{*pitch} : std::vector<VoiceGraph>{};
    Description description = describeVoice(*voice, contents, pitches, named.sampleRate, named.threads);
    description.licenceFile = named.licenceFile;
    return description;
}

} // namespace

std::string localFolder(const std::string &folder)
{
    if (folder != "~" && !folder.starts_with("~/")) {
        return folder;
    }
    const char *const home = std::getenv("HOME");
    return home == nullptr || *home == '\0' ? folder : std::string{home} + folder.substr(1);
}

Description recognise(const std::string &typed)
{
    const std::string folder = localFolder(typed);
    struct stat info{};
    if (folder.empty() || ::stat(folder.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)
        || ::access(folder.c_str(), R_OK | X_OK) != 0) {
        return refused(Refusal::NoFolder);
    }
    const std::vector<std::string> names = namesIn(folder);
    const std::string licence = licenceFileIn(names);

    // **What a person wrote comes first**, over anything the files look like.
    if (std::ranges::find(names, kConfigurationName) != names.end()) {
        const std::string path = folder + "/" + std::string{kConfigurationName};
        const std::optional<std::string> text = readSmallFile(path);
        std::optional<Description> description;
        if (text) {
            description = parseConfiguration(*text);
        }
        if (!description) {
            return refused(Refusal::BadConfiguration, Family::Configured);
        }
        if (description->family == Family::VoiceConversion) {
            Description voice = readVoiceConfiguration(folder, *description);
            if (voice.usable() && voice.licenceFile.empty()) {
                voice.licenceFile = licence;
            }
            return voice;
        }
        const std::string file = folder + "/" + description->modelFile;
        if (!endsWith(lower(description->modelFile), ".onnx") || !isFile(file)) {
            return refused(Refusal::BadConfiguration, Family::Configured);
        }
        const std::expected<OnnxSummary, OnnxFault> summary = readOnnxHead(file, folder);
        if (!summary) {
            return refused(refusalOf(summary.error()), Family::Configured);
        }
        if (!matches(*description, *summary)) {
            return refused(Refusal::WrongShape, Family::Configured);
        }
        if (description->licenceFile.empty()) {
            description->licenceFile = licence;
        }
        return *description;
    }

    // The three graphs of a family that takes them together.
    const auto named = [&names](std::string_view name) { return std::ranges::find(names, name) != names.end(); };
    if (named("enc.onnx") && named("erb_dec.onnx") && named("df_dec.onnx")) {
        return refused(Refusal::NotYetSupported, Family::Cleanup);
    }

    bool sawOnnx = false;
    Refusal fault = Refusal::None;
    std::size_t tried = 0;
    // A voice takes more than one file: which is which shows in their ports.
    std::optional<VoiceGraph> voice;
    std::vector<VoiceGraph> contents;
    std::vector<VoiceGraph> pitches;
    for (const std::string &name : names) {
        if (!endsWith(lower(name), ".onnx") || tried >= kMostOnnxFiles) {
            continue;
        }
        ++tried;
        sawOnnx = true;
        const std::string path = folder + "/" + name;
        const std::expected<OnnxSummary, OnnxFault> summary = readOnnxHead(path, folder);
        if (!summary) {
            fault = refusalOf(summary.error());
            continue;
        }
        const Family family = familyOf(*summary);
        if (family == Family::DeepFilterNetStream) {
            Description description;
            describeDeepFilter(description, name);
            description.licenceFile = licence;
            return description;
        }
        if (family == Family::VoiceConversion) {
            if (!voice) {
                voice = VoiceGraph{name, *summary};
            }
            continue;
        }
        if (family != Family::Unknown) {
            return refused(Refusal::NotYetSupported, family);
        }
        if (isPitchEstimator(*summary)) {
            pitches.push_back(VoiceGraph{name, *summary});
        } else if (isContentEncoder(*summary)) {
            contents.push_back(VoiceGraph{name, *summary});
        }
    }
    if (voice) {
        Description description = describeVoice(*voice, contents, pitches, 0, 0);
        description.licenceFile = licence;
        return description;
    }
    if (!contents.empty() || !pitches.empty()) {
        return refused(Refusal::NoVoiceModel, Family::VoiceConversion);
    }
    if (std::ranges::any_of(names, [&folder](const std::string &name) {
            return endsWith(lower(name), ".gguf") && hasGgufMagic(folder + "/" + name);
        })) {
        return refused(Refusal::NotYetSupported, Family::Speech);
    }
    if (fault != Refusal::None) {
        return refused(fault);
    }
    if (sawOnnx) {
        return refused(Refusal::NeedsConfiguration);
    }
    if (std::ranges::any_of(names, [](const std::string &name) { return isCodeFile(name); })) {
        return refused(Refusal::CodeFiles);
    }
    return refused(Refusal::NothingToLoad);
}

} // namespace rb::model
