// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "onnxbytes.h"

#include <cstring>
#include <span>

namespace rb::test
{

namespace
{

void varint(std::string &out, std::uint64_t value)
{
    while (value >= 0x80) {
        out += static_cast<char>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    out += static_cast<char>(value);
}

void numberField(std::string &out, std::uint32_t field, std::uint64_t value)
{
    varint(out, field << 3);
    varint(out, value);
}

void bytesField(std::string &out, std::uint32_t field, const std::string &bytes)
{
    varint(out, (field << 3) | 2);
    varint(out, bytes.size());
    out += bytes;
}

[[nodiscard]] std::string shapeOf(const std::vector<std::int64_t> &dims)
{
    std::string shape;
    int open = 0;
    for (const std::int64_t size : dims) {
        std::string dim;
        if (size < 0) {
            bytesField(dim, 2, "open" + std::to_string(open++));
        } else {
            numberField(dim, 1, static_cast<std::uint64_t>(size));
        }
        bytesField(shape, 1, dim);
    }
    return shape;
}

[[nodiscard]] std::string valueInfo(const OnnxValue &value)
{
    std::string tensor;
    numberField(tensor, 1, static_cast<std::uint64_t>(value.element));
    bytesField(tensor, 2, shapeOf(value.dims));
    std::string type;
    bytesField(type, 1, tensor);
    std::string info;
    bytesField(info, 1, value.name);
    bytesField(info, 2, type);
    return info;
}

[[nodiscard]] std::string tensorOf(const OnnxWeight &weight)
{
    std::string tensor;
    for (const std::int64_t size : weight.dims) {
        numberField(tensor, 1, static_cast<std::uint64_t>(size));
    }
    numberField(tensor, 2, weight.isInt64 || !weight.integers.empty() ? 7 : (weight.isBool ? 9 : 1));
    bytesField(tensor, 8, weight.name);
    if (!weight.externalLocation.empty()) {
        std::string entry;
        bytesField(entry, 1, "location");
        bytesField(entry, 2, weight.externalLocation);
        bytesField(tensor, 13, entry);
        numberField(tensor, 14, 1);
        return tensor;
    }
    if (!weight.integers.empty()) {
        std::string raw(weight.integers.size() * sizeof(std::int64_t), '\0');
        std::memcpy(raw.data(), weight.integers.data(), raw.size());
        bytesField(tensor, 9, raw);
    } else if (weight.isInt64) {
        std::string raw(sizeof(weight.integer), '\0');
        std::memcpy(raw.data(), &weight.integer, sizeof(weight.integer));
        bytesField(tensor, 9, raw);
    } else if (weight.isBool) {
        bytesField(tensor, 9, std::string(1, '\1'));
    } else {
        std::string raw(weight.floats.size() * sizeof(float), '\0');
        std::memcpy(raw.data(), weight.floats.data(), raw.size());
        bytesField(tensor, 9, raw);
    }
    return tensor;
}

[[nodiscard]] std::string nodeOf(const OnnxNode &node)
{
    std::string out;
    for (const std::string &input : node.inputs) {
        bytesField(out, 1, input);
    }
    for (const std::string &output : node.outputs) {
        bytesField(out, 2, output);
    }
    bytesField(out, 4, node.op);
    out += node.attributes;
    return out;
}

[[nodiscard]] std::string graphBytes(const OnnxGraph &graph)
{
    std::string out;
    for (const OnnxNode &node : graph.nodes) {
        bytesField(out, 1, nodeOf(node));
    }
    bytesField(out, 2, "test");
    for (const OnnxWeight &weight : graph.weights) {
        bytesField(out, 5, tensorOf(weight));
    }
    for (const OnnxValue &value : graph.inputs) {
        bytesField(out, 11, valueInfo(value));
    }
    for (const OnnxValue &value : graph.outputs) {
        bytesField(out, 12, valueInfo(value));
    }
    return out;
}

} // namespace

namespace
{

[[nodiscard]] OnnxWeight
weightOf(const std::string &name, std::vector<std::int64_t> dims, std::vector<float> floats = {})
{
    OnnxWeight weight;
    weight.name = name;
    weight.dims = std::move(dims);
    weight.floats = std::move(floats);
    return weight;
}

[[nodiscard]] OnnxWeight integersOf(const std::string &name, std::vector<std::int64_t> values)
{
    OnnxWeight weight;
    weight.name = name;
    weight.dims = {static_cast<std::int64_t>(values.size())};
    weight.integers = std::move(values);
    return weight;
}

/// An attribute holding whole numbers, ready for a node.
[[nodiscard]] std::string integersAttribute(const std::string &name, const std::vector<std::int64_t> &values)
{
    std::string attribute;
    bytesField(attribute, 1, name);
    for (const std::int64_t value : values) {
        numberField(attribute, 8, static_cast<std::uint64_t>(value));
    }
    numberField(attribute, 20, 7);
    std::string wrapped;
    bytesField(wrapped, 5, attribute);
    return wrapped;
}

/// An attribute holding one whole number, ready for a node.
[[nodiscard]] std::string integerAttribute(const std::string &name, std::int64_t value)
{
    std::string attribute;
    bytesField(attribute, 1, name);
    numberField(attribute, 3, static_cast<std::uint64_t>(value));
    numberField(attribute, 20, 2);
    std::string wrapped;
    bytesField(wrapped, 5, attribute);
    return wrapped;
}

/// Turns the last three of a tensor of 1, a dimension and \a dims so the
/// other two change places.
[[nodiscard]] OnnxNode swapped(const std::string &in, const std::string &out)
{
    return {"Transpose", {in}, {out}, integersAttribute("perm", {0, 2, 1})};
}

/// What \a in becomes, \a fault considered, as \a out: nothing, or all of
/// it not a number, or the same after a loop that takes its time.
void withFault(OnnxGraph &graph, TinyFault fault, const std::string &in, const std::string &out)
{
    if (fault == TinyFault::NotSound) {
        graph.weights.push_back(weightOf("nothing", {}, {0.0f}));
        graph.nodes.push_back({"Mul", {in, "nothing"}, {"faulted"}, {}});
        graph.nodes.push_back({"Div", {"faulted", "nothing"}, {out}, {}});
        return;
    }
    if (fault == TinyFault::Slow) {
        OnnxGraph body;
        body.inputs = {{"turn", {}, 7}, {"going_in", {}, 9}, {"carried", {-1, -1, -1}}};
        body.outputs = {{"going_out", {}, 9}, {"carried_out", {-1, -1, -1}}};
        body.nodes = {{"Identity", {"going_in"}, {"going_out"}, {}}, {"Identity", {"carried"}, {"carried_out"}, {}}};
        std::string attribute;
        bytesField(attribute, 1, "body");
        numberField(attribute, 20, 5);
        bytesField(attribute, 6, graphBytes(body));
        std::string attributes;
        bytesField(attributes, 5, attribute);
        OnnxWeight turns = weightOf("turns", {});
        turns.isInt64 = true;
        // Longer than a voice's hop, whichever of the three holds it.
        turns.integer = 6000000;
        OnnxWeight going = weightOf("going", {});
        going.isBool = true;
        graph.weights.push_back(turns);
        graph.weights.push_back(going);
        graph.nodes.push_back({"Loop", {"turns", "going", in}, {out}, attributes});
        return;
    }
    graph.nodes.push_back({"Identity", {in}, {out}, {}});
}

} // namespace

std::string onnxBytes(const OnnxGraph &graph)
{
    std::string out;
    numberField(out, 1, 8);
    bytesField(out, 7, graphBytes(graph));
    std::string opset;
    numberField(opset, 2, 17);
    bytesField(out, 8, opset);
    return out;
}

std::string gainModel(float gain, std::int64_t frames, int rank, std::int64_t channels)
{
    std::vector<std::int64_t> dims{frames};
    if (rank == 3) {
        dims = {1, 1, frames};
    } else if (channels == 2) {
        dims = {2, frames};
    }
    OnnxGraph graph;
    graph.inputs = {{"audio", dims}};
    graph.outputs = {{"out", dims}};
    graph.weights = {weightOf("gain", {}, {gain})};
    graph.nodes = {{"Mul", {"audio", "gain"}, {"out"}, {}}};
    return onnxBytes(graph);
}

std::string oneHopLateModel(std::int64_t frames)
{
    OnnxGraph graph;
    graph.inputs = {{"audio", {frames}}, {"state_in", {frames}}};
    graph.outputs = {{"out", {frames}}, {"state_out", {frames}}};
    graph.nodes = {{"Identity", {"state_in"}, {"out"}, {}}, {"Identity", {"audio"}, {"state_out"}, {}}};
    return onnxBytes(graph);
}

std::string notSoundModel(std::int64_t frames)
{
    OnnxGraph graph;
    graph.inputs = {{"audio", {frames}}};
    graph.outputs = {{"out", {frames}}};
    graph.weights = {weightOf("zero", {}, {0.0f})};
    graph.nodes = {{"Div", {"audio", "zero"}, {"out"}, {}}};
    return onnxBytes(graph);
}

std::string slowModel(std::int64_t frames, std::int64_t iterations)
{
    // The loop's body: the sound as it was, and on to the next turn.
    OnnxGraph body;
    body.inputs = {{"turn", {}, 7}, {"going_in", {}, 9}, {"carried", {frames}}};
    body.outputs = {{"going_out", {}, 9}, {"carried_out", {frames}}};
    body.nodes = {{"Identity", {"going_in"}, {"going_out"}, {}}, {"Identity", {"carried"}, {"carried_out"}, {}}};

    std::string attribute;
    bytesField(attribute, 1, "body");
    numberField(attribute, 20, 5);
    bytesField(attribute, 6, graphBytes(body));
    std::string attributes;
    bytesField(attributes, 5, attribute);

    OnnxGraph graph;
    graph.inputs = {{"audio", {frames}}};
    graph.outputs = {{"out", {frames}}};
    OnnxWeight turns = weightOf("turns", {});
    turns.isInt64 = true;
    turns.integer = iterations;
    OnnxWeight going = weightOf("going", {});
    going.isBool = true;
    graph.weights = {turns, going};
    graph.nodes = {{"Loop", {"turns", "going", "audio"}, {"out"}, attributes}};
    return onnxBytes(graph);
}

std::string modelWithInputs(const std::vector<std::string> &names)
{
    OnnxGraph graph;
    for (const std::string &name : names) {
        graph.inputs.push_back({name, {1}});
    }
    graph.outputs = {{"out", {1}}};
    graph.nodes = {{"Identity", {names.front()}, {"out"}, {}}};
    return onnxBytes(graph);
}

std::string tinyContentEncoder(TinyFault fault, std::int64_t bands)
{
    OnnxGraph graph;
    graph.inputs = {{"source", {1, 1, -1}}};
    graph.outputs = {{"embed", {1, -1, bands}}};
    graph.weights = {integersOf("wide", {1, bands, 1})};
    const std::int64_t frame = fault == TinyFault::TooLittle ? 640 : 320;
    graph.nodes = {{"AveragePool",
                    {"source"},
                    {"frames"},
                    integersAttribute("kernel_shape", {frame}) + integersAttribute("strides", {frame})},
                   {"Expand", {"frames", "wide"}, {"spread"}, {}}};
    withFault(graph, fault, "spread", "ready");
    graph.nodes.push_back(swapped("ready", "embed"));
    return onnxBytes(graph);
}

std::string tinyMaskedContentEncoder()
{
    OnnxGraph graph;
    graph.inputs = {{"input_values", {-1, -1}}, {"attention_mask", {-1, -1}, 7}};
    graph.outputs = {{"hidden_states", {-1, -1, 768}}};
    graph.weights = {integersOf("wide", {1, 768, 1}), integersOf("channel", {1})};
    graph.nodes = {{"Unsqueeze", {"input_values", "channel"}, {"sound"}, {}},
                   {"AveragePool",
                    {"sound"},
                    {"frames"},
                    integersAttribute("kernel_shape", {320}) + integersAttribute("strides", {320})},
                   {"Expand", {"frames", "wide"}, {"spread"}, {}},
                   swapped("spread", "hidden_states")};
    return onnxBytes(graph);
}

std::string tinyPitchEstimator(std::int64_t likeliest, TinyFault fault)
{
    OnnxGraph graph;
    graph.inputs = {{"input", {1, 128, -1}}};
    graph.outputs = {{"output", {-1, -1, -1}}};
    const std::size_t pitches = fault == TinyFault::TooLittle ? 100 : 360;
    std::vector<float> sure(pitches, 0.0f);
    sure[static_cast<std::size_t>(likeliest) % pitches] = 1.0f;
    graph.weights = {weightOf("sure", {1, 1, static_cast<std::int64_t>(pitches)}, sure),
                     weightOf("no_more", {}, {0.0f})};
    graph.nodes = {
        {"ReduceMean", {"input"}, {"flat"}, integersAttribute("axes", {1}) + integerAttribute("keepdims", 1)},
        {"Mul", {"flat", "no_more"}, {"quiet"}, {}},
        swapped("quiet", "turned"),
        {"Add", {"turned", "sure"}, {"sum"}, {}}};
    withFault(graph, fault, "sum", "output");
    return onnxBytes(graph);
}

std::string tinyVoice(const TinyVoice &voice)
{
    const std::string features = voice.otherNames ? "feats" : "phone";
    OnnxGraph graph;
    graph.inputs = {{features, {1, -1, voice.bands}}, {voice.otherNames ? "p_len" : "phone_lengths", {1}, 7}};
    if (!voice.noPitch) {
        graph.inputs.push_back({"pitch", {1, -1}, 7});
        graph.inputs.push_back({"pitchf", {1, -1}});
    }
    graph.inputs.push_back({voice.otherNames ? "sid" : "ds", {1}, 7});
    if (!voice.otherNames) {
        graph.inputs.push_back({"rnd", {1, 192, -1}});
    }
    graph.outputs = {{"audio", {1, 1, -1}}};
    graph.weights = {
        integersOf("one_row", {1, 1, -1}),
        weightOf("hold", {1, 1, voice.perFrame}, std::vector<float>(static_cast<std::size_t>(voice.perFrame), 1.0f)),
        weightOf("to_khz", {}, {0.001f})};
    if (voice.sayThePitch) {
        graph.nodes = {{"Reshape", {"pitchf", "one_row"}, {"hertz"}, {}}, {"Mul", {"hertz", "to_khz"}, {"said"}, {}}};
    } else {
        graph.nodes = {
            {"ReduceMean", {features}, {"mean"}, integersAttribute("axes", {2}) + integerAttribute("keepdims", 1)},
            swapped("mean", "said")};
    }
    graph.nodes.push_back(
        {"ConvTranspose",
         {"said", "hold"},
         {"held"},
         integersAttribute("kernel_shape", {voice.perFrame}) + integersAttribute("strides", {voice.perFrame})});
    if (voice.fault == TinyFault::TooLittle) {
        graph.nodes.push_back({"Concat", {"held", "held"}, {"audio"}, integerAttribute("axis", 2)});
    } else {
        withFault(graph, voice.fault, "held", "audio");
    }
    return onnxBytes(graph);
}

std::string externalDataModel(const std::string &location)
{
    OnnxGraph graph;
    graph.inputs = {{"audio", {4}}};
    graph.outputs = {{"out", {4}}};
    OnnxWeight weight = weightOf("gain", {4});
    weight.externalLocation = location;
    graph.weights = {weight};
    graph.nodes = {{"Mul", {"audio", "gain"}, {"out"}, {}}};
    return onnxBytes(graph);
}

} // namespace rb::test
