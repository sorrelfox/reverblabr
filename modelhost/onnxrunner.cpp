// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "onnxrunner.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <numeric>
#include <optional>
#include <string_view>

namespace rb::modelhost
{

namespace
{

/// The providers worth asking for, best first. CPU is always there.
constexpr std::array<std::string_view, 3> kGpuProviders{"ROCMExecutionProvider",
                                                        "MIGraphXExecutionProvider",
                                                        "CUDAExecutionProvider"};

/// The most numbers one state tensor may hold.
constexpr std::int64_t kMostStateElements = 1 << 25;

/// Whether \a dims can be what \a wanted are: a size left open fits anything.
[[nodiscard]] bool fits(const std::vector<std::int64_t> &dims, const std::vector<std::int64_t> &wanted)
{
    if (dims.size() != wanted.size()) {
        return false;
    }
    for (std::size_t index = 0; index < dims.size(); ++index) {
        if (dims[index] > 0 && dims[index] != wanted[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

struct OnnxRunner::Private {
    model::Description description;
    Ort::Env env{ORT_LOGGING_LEVEL_FATAL, "reverblabr-model-host"};
    std::optional<Ort::Session> session;
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    model::Refusal refusal = model::Refusal::None;
    model::Device device = model::Device::Cpu;

    std::vector<std::int64_t> inputShape;
    std::vector<std::vector<std::int64_t>> stateShapes;
    std::vector<std::vector<std::int64_t>> constantShapes;
    std::vector<float> constantValues;
    /// Per stream, per state: the numbers.
    std::vector<std::vector<std::vector<float>>> states;
    std::vector<const char *> inputNames;
    std::vector<const char *> outputNames;

    [[nodiscard]] Ort::SessionOptions options()
    {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(static_cast<int>(description.threads));
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Waiting by spinning would burn a core between runs, on a machine
        // that has real-time work to do.
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
        options.AddConfigEntry("session.inter_op.allow_spinning", "0");
        const std::vector<std::string> available = Ort::GetAvailableProviders();
        for (const std::string_view wanted : kGpuProviders) {
            if (std::ranges::find(available, wanted) == available.end()) {
                continue;
            }
            try {
                options.AppendExecutionProvider(std::string{wanted});
                device = model::Device::Gpu;
                break;
            } catch (const Ort::Exception &) {
                // CPU it is, or the next one.
            }
        }
        return options;
    }

    /// The input's shape for what the model declares, or nothing.
    [[nodiscard]] std::optional<std::vector<std::int64_t>> audioShape(const std::vector<std::int64_t> &declared) const
    {
        const std::int64_t channels = description.channels;
        const std::int64_t frames = description.frameSize;
        std::vector<std::int64_t> shape;
        switch (declared.size()) {
        case 1:
            if (channels != 1) {
                return std::nullopt;
            }
            shape = {frames};
            break;
        case 2:
            shape = {channels, frames};
            break;
        case 3:
            shape = {1, channels, frames};
            break;
        default:
            return std::nullopt;
        }
        return fits(declared, shape) ? std::optional{shape} : std::nullopt;
    }

    void describeInputs()
    {
        Ort::AllocatorWithDefaultOptions allocator;
        std::optional<std::vector<std::int64_t>> audio;
        const std::size_t count = session->GetInputCount();
        stateShapes.resize(description.state.size());
        constantShapes.resize(description.constants.size());
        constantValues.resize(description.constants.size());
        for (std::size_t index = 0; index < count; ++index) {
            const Ort::AllocatedStringPtr name = session->GetInputNameAllocated(index, allocator);
            const Ort::TypeInfo type = session->GetInputTypeInfo(index);
            if (type.GetONNXType() != ONNX_TYPE_TENSOR) {
                refusal = model::Refusal::WrongShape;
                return;
            }
            const auto info = type.GetTensorTypeAndShapeInfo();
            const std::vector<std::int64_t> dims = info.GetShape();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                refusal = model::Refusal::WrongShape;
                return;
            }
            const std::string_view named{name.get()};
            if (named == description.input) {
                audio = audioShape(dims);
                continue;
            }
            for (std::size_t state = 0; state < description.state.size(); ++state) {
                if (named == description.state[state].input) {
                    std::int64_t elements = 1;
                    for (const std::int64_t size : dims) {
                        if (size <= 0 || size > kMostStateElements || elements * size > kMostStateElements) {
                            refusal = model::Refusal::WrongShape;
                            return;
                        }
                        elements *= size;
                    }
                    stateShapes[state] = dims;
                }
            }
            for (std::size_t constant = 0; constant < description.constants.size(); ++constant) {
                if (named == description.constants[constant].input) {
                    if (dims.size() > 1 || (dims.size() == 1 && dims[0] > 1)) {
                        refusal = model::Refusal::WrongShape;
                        return;
                    }
                    constantShapes[constant] =
                        dims.empty() ? std::vector<std::int64_t>{} : std::vector<std::int64_t>{1};
                    constantValues[constant] = description.constants[constant].value;
                }
            }
        }
        if (!audio) {
            refusal = model::Refusal::WrongShape;
            return;
        }
        inputShape = *audio;
    }

    void prepareStates(std::size_t streams)
    {
        states.assign(streams, {});
        for (auto &stream : states) {
            for (const std::vector<std::int64_t> &shape : stateShapes) {
                const std::int64_t elements =
                    std::accumulate(shape.begin(), shape.end(), std::int64_t{1}, std::multiplies<>{});
                stream.emplace_back(static_cast<std::size_t>(elements), 0.0f);
            }
        }
        inputNames.push_back(description.input.c_str());
        for (const model::StatePair &pair : description.state) {
            inputNames.push_back(pair.input.c_str());
        }
        for (const model::Constant &constant : description.constants) {
            inputNames.push_back(constant.input.c_str());
        }
        outputNames.push_back(description.output.c_str());
        for (const model::StatePair &pair : description.state) {
            outputNames.push_back(pair.output.c_str());
        }
    }
};

OnnxRunner::OnnxRunner(const std::string &folder, const model::Description &description, std::size_t streams)
    : d(std::make_unique<Private>())
{
    d->description = description;
    try {
        const std::string path = folder + "/" + description.modelFile;
        Ort::SessionOptions options = d->options();
        d->session.emplace(d->env, path.c_str(), options);
        d->describeInputs();
        if (d->refusal == model::Refusal::None) {
            d->prepareStates(streams);
        }
    } catch (const std::exception &) {
        d->refusal = model::Refusal::RuntimeRefused;
    }
}

OnnxRunner::~OnnxRunner() = default;

model::Refusal OnnxRunner::refusal() const noexcept
{
    return d->refusal;
}

model::Device OnnxRunner::device() const noexcept
{
    return d->device;
}

void OnnxRunner::clearState()
{
    for (auto &stream : d->states) {
        for (std::vector<float> &state : stream) {
            std::ranges::fill(state, 0.0f);
        }
    }
}

bool OnnxRunner::run(std::size_t stream, std::span<const float> input, std::span<float> output)
{
    const model::Description &description = d->description;
    std::vector<Ort::Value> values;
    values.reserve(d->inputNames.size());
    values.push_back(Ort::Value::CreateTensor<float>(
        d->memory, const_cast<float *>(input.data()), input.size(), d->inputShape.data(), d->inputShape.size()));
    std::vector<std::vector<float>> &states = d->states[stream];
    for (std::size_t state = 0; state < states.size(); ++state) {
        values.push_back(Ort::Value::CreateTensor<float>(d->memory,
                                                         states[state].data(),
                                                         states[state].size(),
                                                         d->stateShapes[state].data(),
                                                         d->stateShapes[state].size()));
    }
    for (std::size_t constant = 0; constant < d->constantValues.size(); ++constant) {
        values.push_back(Ort::Value::CreateTensor<float>(d->memory,
                                                         &d->constantValues[constant],
                                                         1,
                                                         d->constantShapes[constant].data(),
                                                         d->constantShapes[constant].size()));
    }
    std::vector<Ort::Value> results = d->session->Run(Ort::RunOptions{nullptr},
                                                      d->inputNames.data(),
                                                      values.data(),
                                                      values.size(),
                                                      d->outputNames.data(),
                                                      d->outputNames.size());

    // **What came back is held against what was promised** before a sample
    // of it is copied.
    const auto info = results[0].GetTensorTypeAndShapeInfo();
    const std::size_t channels = description.channels;
    const std::size_t count = info.GetElementCount();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
        || (count != channels * description.frameSize && count != channels * description.hopSize)
        || output.size() != channels * description.hopSize) {
        return false;
    }
    const float *const from = results[0].GetTensorData<float>();
    const std::size_t length = count / channels;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        // The last hop of each channel, whichever length came back.
        std::copy_n(from + channel * length + (length - description.hopSize),
                    description.hopSize,
                    output.begin() + static_cast<std::ptrdiff_t>(channel * description.hopSize));
    }
    for (std::size_t state = 0; state < states.size(); ++state) {
        const auto stateInfo = results[state + 1].GetTensorTypeAndShapeInfo();
        if (stateInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
            || stateInfo.GetElementCount() != states[state].size()) {
            return false;
        }
        std::copy_n(results[state + 1].GetTensorData<float>(), states[state].size(), states[state].begin());
    }
    return true;
}

} // namespace rb::modelhost
