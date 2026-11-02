// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model/onnxreader.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <span>
#include <string_view>

namespace rb::model
{

namespace
{

/// A graph inside a node inside a graph, this deep at most.
constexpr int kDeepestGraph = 16;
constexpr std::size_t kMostPorts = 4096;
constexpr std::size_t kMostDims = 32;
/// The IR versions the format has had, with room for those to come.
constexpr std::uint64_t kNewestIr = 64;
/// More upsampling than any graph of sound has: past this it stops counting.
constexpr std::uint64_t kMostUpsampling = 1ULL << 20;

// The field numbers of the format's messages this reader looks at.
constexpr std::uint32_t kModelIrVersion = 1;
constexpr std::uint32_t kModelGraph = 7;
constexpr std::uint32_t kGraphNode = 1;
constexpr std::uint32_t kGraphInitializer = 5;
constexpr std::uint32_t kGraphInput = 11;
constexpr std::uint32_t kGraphOutput = 12;
constexpr std::uint32_t kNodeOperation = 4;
constexpr std::uint32_t kNodeAttribute = 5;
constexpr std::uint32_t kAttributeName = 1;
constexpr std::uint32_t kAttributeTensor = 5;
constexpr std::uint32_t kAttributeGraph = 6;
constexpr std::uint32_t kAttributeInts = 8;
constexpr std::uint32_t kAttributeTensors = 10;
constexpr std::uint32_t kAttributeGraphs = 11;
constexpr std::uint32_t kTensorName = 8;
constexpr std::uint32_t kTensorExternalData = 13;
constexpr std::uint32_t kEntryKey = 1;
constexpr std::uint32_t kEntryValue = 2;
constexpr std::uint32_t kValueInfoName = 1;
constexpr std::uint32_t kValueInfoType = 2;
constexpr std::uint32_t kTypeTensor = 1;
constexpr std::uint32_t kTensorTypeElement = 1;
constexpr std::uint32_t kTensorTypeShape = 2;
constexpr std::uint32_t kShapeDim = 1;
constexpr std::uint32_t kDimValue = 1;

constexpr std::uint32_t kWireVarint = 0;
constexpr std::uint32_t kWire64 = 1;
constexpr std::uint32_t kWireBytes = 2;
constexpr std::uint32_t kWire32 = 5;

/// One message's bytes, read from the front. A fault sticks.
class Wire
{
public:
    explicit Wire(std::span<const std::byte> bytes)
        : m_bytes(bytes)
    {
    }

    /// Whether another field follows.
    [[nodiscard]] bool more() const noexcept
    {
        return !m_fault && m_at < m_bytes.size();
    }

    [[nodiscard]] bool faulty() const noexcept
    {
        return m_fault;
    }

    [[nodiscard]] std::uint64_t varint() noexcept
    {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_at >= m_bytes.size()) {
                break;
            }
            const auto byte = std::to_integer<std::uint8_t>(m_bytes[m_at++]);
            value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return value;
            }
        }
        m_fault = true;
        return 0;
    }

    /// The next field's number and wire type.
    [[nodiscard]] std::uint32_t field(std::uint32_t &wire) noexcept
    {
        const std::uint64_t tag = varint();
        wire = static_cast<std::uint32_t>(tag & 7);
        if (tag >> 3 == 0 || tag >> 3 > 0xFFFFFFFFU) {
            m_fault = true;
        }
        return static_cast<std::uint32_t>(tag >> 3);
    }

    /// A length-delimited field's bytes, which have to be there.
    [[nodiscard]] std::span<const std::byte> bytes() noexcept
    {
        const std::uint64_t length = varint();
        if (m_fault || length > m_bytes.size() - m_at) {
            m_fault = true;
            return {};
        }
        const std::span<const std::byte> found = m_bytes.subspan(m_at, static_cast<std::size_t>(length));
        m_at += static_cast<std::size_t>(length);
        return found;
    }

    /// Past a field that is not needed. Groups never appear in this format.
    void skip(std::uint32_t wire) noexcept
    {
        switch (wire) {
        case kWireVarint:
            (void)varint();
            break;
        case kWire64:
            advance(8);
            break;
        case kWireBytes:
            (void)bytes();
            break;
        case kWire32:
            advance(4);
            break;
        default:
            m_fault = true;
            break;
        }
    }

private:
    void advance(std::size_t count) noexcept
    {
        if (count > m_bytes.size() - m_at) {
            m_fault = true;
            return;
        }
        m_at += count;
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_at = 0;
    bool m_fault = false;
};

[[nodiscard]] std::string textOf(std::span<const std::byte> bytes)
{
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

/// Whether \a location names a file inside \a folder, by its words first
/// and then by where it really is: a symbolic link may lead out.
[[nodiscard]] bool insideFolder(const std::string &location, const std::string &folder)
{
    if (location.empty() || location.front() == '/' || location.find('\\') != std::string::npos) {
        return false;
    }
    std::size_t at = 0;
    while (at <= location.size()) {
        const std::size_t end = std::min(location.find('/', at), location.size());
        if (location.compare(at, end - at, "..") == 0) {
            return false;
        }
        at = end + 1;
    }
    const std::string whole = folder + "/" + location;
    char realFolder[4096];
    char realFile[4096];
    if (::realpath(folder.c_str(), realFolder) == nullptr || ::realpath(whole.c_str(), realFile) == nullptr) {
        return false;
    }
    const std::string_view within{realFile};
    const std::string prefix = std::string{realFolder} + "/";
    return within.starts_with(prefix);
}

/// What a walk over one file found.
struct Walk {
    const std::string &folder;
    bool unsafe = false;
    bool faulty = false;
};

void walkTensor(std::span<const std::byte> bytes, Walk &walk, std::string *name)
{
    Wire wire(bytes);
    while (wire.more()) {
        std::uint32_t type = 0;
        const std::uint32_t number = wire.field(type);
        if (number == kTensorName && type == kWireBytes && name != nullptr) {
            *name = textOf(wire.bytes());
        } else if (number == kTensorExternalData && type == kWireBytes) {
            Wire entry(wire.bytes());
            std::string key;
            std::string value;
            while (entry.more()) {
                std::uint32_t entryType = 0;
                const std::uint32_t entryNumber = entry.field(entryType);
                if (entryNumber == kEntryKey && entryType == kWireBytes) {
                    key = textOf(entry.bytes());
                } else if (entryNumber == kEntryValue && entryType == kWireBytes) {
                    value = textOf(entry.bytes());
                } else {
                    entry.skip(entryType);
                }
            }
            walk.faulty = walk.faulty || entry.faulty();
            if (key == "location" && !insideFolder(value, walk.folder)) {
                walk.unsafe = true;
            }
        } else {
            wire.skip(type);
        }
    }
    walk.faulty = walk.faulty || wire.faulty();
}

[[nodiscard]] std::optional<OnnxPort> readPort(std::span<const std::byte> bytes, Walk &walk)
{
    OnnxPort port;
    Wire info(bytes);
    while (info.more()) {
        std::uint32_t type = 0;
        const std::uint32_t number = info.field(type);
        if (number == kValueInfoName && type == kWireBytes) {
            port.name = textOf(info.bytes());
        } else if (number == kValueInfoType && type == kWireBytes) {
            Wire typeProto(info.bytes());
            while (typeProto.more()) {
                std::uint32_t kind = 0;
                if (typeProto.field(kind) != kTypeTensor || kind != kWireBytes) {
                    typeProto.skip(kind);
                    continue;
                }
                Wire tensor(typeProto.bytes());
                while (tensor.more()) {
                    std::uint32_t tensorWire = 0;
                    const std::uint32_t tensorField = tensor.field(tensorWire);
                    if (tensorField == kTensorTypeElement && tensorWire == kWireVarint) {
                        port.elementType = static_cast<std::int32_t>(tensor.varint());
                    } else if (tensorField == kTensorTypeShape && tensorWire == kWireBytes) {
                        Wire shape(tensor.bytes());
                        while (shape.more()) {
                            std::uint32_t shapeWire = 0;
                            if (shape.field(shapeWire) != kShapeDim || shapeWire != kWireBytes) {
                                shape.skip(shapeWire);
                                continue;
                            }
                            Wire dim(shape.bytes());
                            std::int64_t size = -1;
                            while (dim.more()) {
                                std::uint32_t dimWire = 0;
                                if (dim.field(dimWire) == kDimValue && dimWire == kWireVarint) {
                                    size = static_cast<std::int64_t>(dim.varint());
                                } else {
                                    dim.skip(dimWire);
                                }
                            }
                            walk.faulty = walk.faulty || dim.faulty() || port.dims.size() >= kMostDims;
                            port.dims.push_back(size);
                        }
                        walk.faulty = walk.faulty || shape.faulty();
                    } else {
                        tensor.skip(tensorWire);
                    }
                }
                walk.faulty = walk.faulty || tensor.faulty();
            }
            walk.faulty = walk.faulty || typeProto.faulty();
        } else {
            info.skip(type);
        }
    }
    walk.faulty = walk.faulty || info.faulty();
    if (port.name.empty()) {
        walk.faulty = true;
        return std::nullopt;
    }
    return port;
}

void walkGraph(std::span<const std::byte> bytes, Walk &walk, OnnxSummary *summary, int depth);

/// A node's attributes, for what can hold a tensor or a graph of its own,
/// and for the stride of a transposed convolution, which goes into
/// \a upsampling when the node is in the top graph.
void walkNode(std::span<const std::byte> bytes, Walk &walk, int depth, std::uint64_t *upsampling)
{
    bool transposed = false;
    std::uint64_t stride = 1;
    Wire node(bytes);
    while (node.more()) {
        std::uint32_t type = 0;
        const std::uint32_t field = node.field(type);
        if (field == kNodeOperation && type == kWireBytes) {
            transposed = textOf(node.bytes()) == "ConvTranspose";
            continue;
        }
        if (field != kNodeAttribute || type != kWireBytes) {
            node.skip(type);
            continue;
        }
        Wire attribute(node.bytes());
        bool strides = false;
        while (attribute.more()) {
            std::uint32_t kind = 0;
            const std::uint32_t number = attribute.field(kind);
            if (number == kAttributeName && kind == kWireBytes) {
                strides = textOf(attribute.bytes()) == "strides";
            } else if (number == kAttributeInts && strides && kind == kWireVarint) {
                stride = std::clamp<std::uint64_t>(attribute.varint(), 1, kMostUpsampling);
            } else if ((number == kAttributeTensor || number == kAttributeTensors) && kind == kWireBytes) {
                walkTensor(attribute.bytes(), walk, nullptr);
            } else if ((number == kAttributeGraph || number == kAttributeGraphs) && kind == kWireBytes) {
                walkGraph(attribute.bytes(), walk, nullptr, depth + 1);
            } else {
                attribute.skip(kind);
            }
        }
        walk.faulty = walk.faulty || attribute.faulty();
    }
    walk.faulty = walk.faulty || node.faulty();
    if (transposed && upsampling != nullptr) {
        *upsampling = std::min(*upsampling * stride, kMostUpsampling);
    }
}

void walkGraph(std::span<const std::byte> bytes, Walk &walk, OnnxSummary *summary, int depth)
{
    if (depth > kDeepestGraph) {
        walk.faulty = true;
        return;
    }
    std::set<std::string> initialisers;
    Wire graph(bytes);
    while (graph.more()) {
        std::uint32_t type = 0;
        const std::uint32_t number = graph.field(type);
        if (type != kWireBytes) {
            graph.skip(type);
        } else if (number == kGraphNode) {
            walkNode(graph.bytes(), walk, depth, summary != nullptr ? &summary->upsampling : nullptr);
        } else if (number == kGraphInitializer) {
            std::string name;
            walkTensor(graph.bytes(), walk, &name);
            initialisers.insert(std::move(name));
        } else if ((number == kGraphInput || number == kGraphOutput) && summary != nullptr) {
            std::vector<OnnxPort> &ports = number == kGraphInput ? summary->inputs : summary->outputs;
            if (ports.size() >= kMostPorts) {
                walk.faulty = true;
                return;
            }
            if (std::optional<OnnxPort> port = readPort(graph.bytes(), walk)) {
                ports.push_back(std::move(*port));
            }
        } else {
            graph.skip(type);
        }
    }
    walk.faulty = walk.faulty || graph.faulty();
    // An old file lists its weights as inputs too; they are not what a
    // caller has to give.
    if (summary != nullptr) {
        std::erase_if(summary->inputs,
                      [&initialisers](const OnnxPort &port) { return initialisers.contains(port.name); });
    }
}

/// A file mapped for reading: a model can be far larger than memory should
/// hold, and this looks at little of it.
class MappedFile
{
public:
    MappedFile(int fd, std::size_t size)
    {
        void *memory = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (memory != MAP_FAILED) {
            m_memory = memory;
            m_size = size;
        }
    }

    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;

    ~MappedFile()
    {
        if (m_memory != nullptr) {
            ::munmap(m_memory, m_size);
        }
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return {static_cast<const std::byte *>(m_memory), m_memory != nullptr ? m_size : 0};
    }

private:
    void *m_memory = nullptr;
    std::size_t m_size = 0;
};

} // namespace

std::expected<OnnxSummary, OnnxFault> readOnnxHead(const std::string &path, const std::string &folder)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(OnnxFault::Unreadable);
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(fd);
        return std::unexpected(OnnxFault::Unreadable);
    }
    if (static_cast<std::uint64_t>(info.st_size) > kMostModelBytes) {
        ::close(fd);
        return std::unexpected(OnnxFault::TooLarge);
    }
    if (info.st_size == 0) {
        ::close(fd);
        return std::unexpected(OnnxFault::NotOnnx);
    }
    const MappedFile file(fd, static_cast<std::size_t>(info.st_size));
    ::close(fd);
    if (file.bytes().empty()) {
        return std::unexpected(OnnxFault::Unreadable);
    }

    OnnxSummary summary;
    Walk walk{folder};
    bool sawVersion = false;
    bool sawGraph = false;
    Wire model(file.bytes());
    while (model.more()) {
        std::uint32_t type = 0;
        const std::uint32_t number = model.field(type);
        if (number == kModelIrVersion && type == kWireVarint) {
            const std::uint64_t version = model.varint();
            sawVersion = version > 0 && version <= kNewestIr;
        } else if (number == kModelGraph && type == kWireBytes) {
            sawGraph = true;
            walkGraph(model.bytes(), walk, &summary, 0);
        } else {
            model.skip(type);
        }
    }
    if (model.faulty() || walk.faulty || !sawVersion || !sawGraph || summary.outputs.empty()) {
        return std::unexpected(OnnxFault::NotOnnx);
    }
    if (walk.unsafe) {
        return std::unexpected(OnnxFault::UnsafeReference);
    }
    return summary;
}

} // namespace rb::model
