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
    for (const std::int64_t size : dims) {
        std::string dim;
        numberField(dim, 1, static_cast<std::uint64_t>(size));
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
    numberField(tensor, 2, weight.isInt64 ? 7 : (weight.isBool ? 9 : 1));
    bytesField(tensor, 8, weight.name);
    if (!weight.externalLocation.empty()) {
        std::string entry;
        bytesField(entry, 1, "location");
        bytesField(entry, 2, weight.externalLocation);
        bytesField(tensor, 13, entry);
        numberField(tensor, 14, 1);
        return tensor;
    }
    if (weight.isInt64) {
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
