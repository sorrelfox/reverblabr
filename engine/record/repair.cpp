// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/repair.h"

#include <signal.h>
#include <sndfile.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rb::record
{

namespace
{

constexpr std::uint64_t kMax32 = std::numeric_limits<std::uint32_t>::max();

/// A file opened for reading and writing its header in place.
class Header
{
public:
    explicit Header(const std::filesystem::path &path)
        : m_stream(path, std::ios::in | std::ios::out | std::ios::binary)
    {
        std::error_code error;
        m_size = std::filesystem::file_size(path, error);
        if (error) {
            m_stream.close();
        }
    }

    [[nodiscard]] bool isOpen() const
    {
        return m_stream.is_open();
    }

    [[nodiscard]] std::uint64_t size() const
    {
        return m_size;
    }

    [[nodiscard]] bool read(std::uint64_t at, std::span<unsigned char> into)
    {
        if (at + into.size() > m_size) {
            return false;
        }
        m_stream.seekg(static_cast<std::streamoff>(at));
        m_stream.read(reinterpret_cast<char *>(into.data()), static_cast<std::streamsize>(into.size()));
        return static_cast<bool>(m_stream);
    }

    [[nodiscard]] bool write(std::uint64_t at, std::span<const unsigned char> bytes)
    {
        m_stream.seekp(static_cast<std::streamoff>(at));
        m_stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(m_stream);
    }

    [[nodiscard]] std::string tag(std::uint64_t at)
    {
        std::array<unsigned char, 4> bytes{};
        if (!read(at, bytes)) {
            return {};
        }
        return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }

    [[nodiscard]] std::uint64_t little(std::uint64_t at, std::size_t width)
    {
        std::array<unsigned char, 8> bytes{};
        if (!read(at, std::span{bytes}.first(width))) {
            return 0;
        }
        std::uint64_t value = 0;
        for (std::size_t index = width; index-- > 0;) {
            value = (value << 8) | bytes[index];
        }
        return value;
    }

    [[nodiscard]] std::uint64_t big(std::uint64_t at, std::size_t width)
    {
        std::array<unsigned char, 8> bytes{};
        if (!read(at, std::span{bytes}.first(width))) {
            return 0;
        }
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < width; ++index) {
            value = (value << 8) | bytes[index];
        }
        return value;
    }

    [[nodiscard]] bool putLittle(std::uint64_t at, std::uint64_t value, std::size_t width)
    {
        std::array<unsigned char, 8> bytes{};
        for (std::size_t index = 0; index < width; ++index) {
            bytes[index] = static_cast<unsigned char>(value >> (8 * index));
        }
        return write(at, std::span{bytes}.first(width));
    }

    [[nodiscard]] bool putBig(std::uint64_t at, std::uint64_t value, std::size_t width)
    {
        std::array<unsigned char, 8> bytes{};
        for (std::size_t index = 0; index < width; ++index) {
            bytes[width - 1 - index] = static_cast<unsigned char>(value >> (8 * index));
        }
        return write(at, std::span{bytes}.first(width));
    }

    [[nodiscard]] bool putTag(std::uint64_t at, std::string_view tag)
    {
        return write(at, std::span{reinterpret_cast<const unsigned char *>(tag.data()), tag.size()});
    }

    [[nodiscard]] bool flush()
    {
        m_stream.flush();
        return static_cast<bool>(m_stream);
    }

private:
    std::fstream m_stream;
    std::uint64_t m_size = 0;
};

/// Cuts the file at \a end, or pads it to it, so nothing follows the last
/// whole frame.
[[nodiscard]] bool resizeTo(const std::filesystem::path &path, std::uint64_t end)
{
    std::error_code error;
    std::filesystem::resize_file(path, end, error);
    return !error;
}

[[nodiscard]] std::expected<RepairResult, RepairError> repairWav(const std::filesystem::path &path)
{
    std::uint64_t end = 0;
    RepairResult result;
    {
        Header file(path);
        if (!file.isOpen()) {
            return std::unexpected(RepairError::NotReadable);
        }
        const std::string magic = file.tag(0);
        std::uint64_t ds64 = 0;
        std::uint64_t fmt = 0;
        std::uint64_t fmtSize = 0;
        std::uint64_t fact = 0;
        std::uint64_t data = 0;
        std::uint64_t blockAlign = 0;
        for (std::uint64_t at = 12; at + 8 <= file.size();) {
            const std::string id = file.tag(at);
            const std::uint64_t size = file.little(at + 4, 4);
            if (id == "ds64") {
                ds64 = at + 8;
            } else if (id == "fmt ") {
                fmt = at;
                fmtSize = size;
                blockAlign = file.little(at + 8 + 12, 2);
            } else if (id == "fact") {
                fact = at + 8;
            } else if (id == "data") {
                // **The last chunk of a recording that was cut off**, and the
                // one whose size is wrong, so the walk stops here rather
                // than trust it.
                data = at + 8;
                break;
            }
            at += 8 + size + (size & 1U);
        }
        if (data == 0 || blockAlign == 0) {
            return std::unexpected(RepairError::NotRecognised);
        }

        const std::uint64_t bytes = (file.size() - data) / blockAlign * blockAlign;
        result.frames = bytes / blockAlign;
        end = data + bytes + (bytes & 1U); // a chunk is padded to an even length
        const std::uint64_t riffSize = end - 8;
        const bool wide = magic == "RF64" || riffSize > kMax32;

        bool ok = true;
        if (wide && ds64 == 0) {
            // **Past 4 GB a WAV has to become RF64**, and one whose header was
            // last written while it was smaller is laid out as a plain WAV:
            // a placeholder where the ds64 chunk would go, the format, and a
            // fact chunk. The audio cannot move, so the header is laid out
            // again in front of it: ds64, the format as it was, and a JUNK
            // chunk over whatever room is left. Everything else before the
            // audio — the fact chunk, a peak chunk — is optional, and RF64
            // readers want the ds64 chunk first.
            constexpr std::uint64_t kDs64Bytes = 8 + 28;
            const std::uint64_t fmtBytes = 8 + fmtSize + (fmtSize & 1U);
            const std::uint64_t room = (data - 8) - 12;
            if (fmt == 0 || room < kDs64Bytes + fmtBytes
                || (room - kDs64Bytes - fmtBytes != 0 && room - kDs64Bytes - fmtBytes < 8)) {
                return std::unexpected(RepairError::NotRecognised);
            }
            std::vector<unsigned char> format(static_cast<std::size_t>(fmtBytes));
            if (!file.read(fmt, format)) {
                return std::unexpected(RepairError::NotReadable);
            }
            std::vector<unsigned char> header(static_cast<std::size_t>(room), 0);
            const auto put = [&header](std::size_t at, std::uint64_t value, std::size_t width) {
                for (std::size_t index = 0; index < width; ++index) {
                    header[at + index] = static_cast<unsigned char>(value >> (8 * index));
                }
            };
            const auto tag = [&header](std::size_t at, std::string_view name) {
                std::copy(name.begin(), name.end(), header.begin() + static_cast<std::ptrdiff_t>(at));
            };
            tag(0, "ds64");
            put(4, 28, 4);
            std::copy(format.begin(), format.end(), header.begin() + static_cast<std::ptrdiff_t>(kDs64Bytes));
            const std::uint64_t spare = room - kDs64Bytes - fmtBytes;
            if (spare != 0) {
                tag(static_cast<std::size_t>(kDs64Bytes + fmtBytes), "JUNK");
                put(static_cast<std::size_t>(kDs64Bytes + fmtBytes + 4), spare - 8, 4);
            }
            ok = ok && file.write(12, header);
            ds64 = 12 + 8;
            fact = 0;
        }
        if (wide) {
            const bool same = magic == "RF64" && file.little(ds64, 8) == riffSize && file.little(ds64 + 8, 8) == bytes
                && file.little(ds64 + 16, 8) == result.frames;
            result.changed = !same;
            ok = ok && file.putTag(0, "RF64") && file.putLittle(4, kMax32, 4) && file.putLittle(data - 4, kMax32, 4);
            ok = ok && file.putLittle(ds64, riffSize, 8) && file.putLittle(ds64 + 8, bytes, 8)
                && file.putLittle(ds64 + 16, result.frames, 8);
        } else {
            result.changed = file.little(4, 4) != riffSize || file.little(data - 4, 4) != bytes;
            ok = ok && file.putLittle(4, riffSize, 4) && file.putLittle(data - 4, bytes, 4);
        }
        if (fact != 0) {
            const std::uint64_t count = std::min(result.frames, kMax32);
            result.changed = result.changed || file.little(fact, 4) != count;
            ok = ok && file.putLittle(fact, count, 4);
        }
        result.changed = result.changed || file.size() != end;
        if (!ok || !file.flush()) {
            return std::unexpected(RepairError::NotWritable);
        }
    }
    if (!resizeTo(path, end)) {
        return std::unexpected(RepairError::NotWritable);
    }
    return result;
}

[[nodiscard]] std::expected<RepairResult, RepairError> repairAiff(const std::filesystem::path &path)
{
    std::uint64_t end = 0;
    RepairResult result;
    {
        Header file(path);
        if (!file.isOpen()) {
            return std::unexpected(RepairError::NotReadable);
        }
        std::uint64_t comm = 0;
        std::uint64_t ssnd = 0;
        for (std::uint64_t at = 12; at + 8 <= file.size();) {
            const std::string id = file.tag(at);
            const std::uint64_t size = file.big(at + 4, 4);
            if (id == "COMM") {
                comm = at + 8;
            } else if (id == "SSND") {
                ssnd = at;
                break; // last, and the one whose size is wrong
            }
            at += 8 + size + (size & 1U);
        }
        if (comm == 0 || ssnd == 0) {
            return std::unexpected(RepairError::NotRecognised);
        }
        const std::uint64_t channels = file.big(comm, 2);
        const std::uint64_t bits = file.big(comm + 6, 2);
        const std::uint64_t offset = file.big(ssnd + 8, 4);
        const std::uint64_t frameBytes = channels * ((bits + 7) / 8);
        const std::uint64_t start = ssnd + 16 + offset;
        if (frameBytes == 0 || start > file.size()) {
            return std::unexpected(RepairError::NotRecognised);
        }
        const std::uint64_t bytes = (file.size() - start) / frameBytes * frameBytes;
        result.frames = bytes / frameBytes;
        const std::uint64_t ssndSize = 8 + offset + bytes;
        end = ssnd + 8 + ssndSize + (ssndSize & 1U);
        const std::uint64_t formSize = end - 8;
        if (formSize > kMax32) {
            return std::unexpected(RepairError::NotRecognised);
        }
        result.changed = file.big(4, 4) != formSize || file.big(ssnd + 4, 4) != ssndSize
            || file.big(comm + 2, 4) != result.frames || file.size() != end;
        const bool ok = file.putBig(4, formSize, 4) && file.putBig(ssnd + 4, ssndSize, 4)
            && file.putBig(comm + 2, result.frames, 4);
        if (!ok || !file.flush()) {
            return std::unexpected(RepairError::NotWritable);
        }
    }
    if (!resizeTo(path, end)) {
        return std::unexpected(RepairError::NotWritable);
    }
    return result;
}

/// How many frames of a FLAC file decode, which is the only way to know
/// when the encoder never got to say.
[[nodiscard]] std::uint64_t decodableFrames(const std::filesystem::path &path)
{
    SF_INFO info{};
    SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
    if (file == nullptr) {
        return 0;
    }
    std::vector<float> buffer(4096 * static_cast<std::size_t>(std::max(info.channels, 1)));
    std::uint64_t frames = 0;
    for (;;) {
        const sf_count_t got = sf_readf_float(file, buffer.data(), 4096);
        if (got <= 0) {
            break;
        }
        frames += static_cast<std::uint64_t>(got);
    }
    sf_close(file);
    return frames;
}

[[nodiscard]] std::expected<RepairResult, RepairError> repairFlac(const std::filesystem::path &path)
{
    // STREAMINFO is always the first block; its total is the low 36 bits of
    // the eight bytes at 10 into it, after the rate, channels and bits.
    constexpr std::uint64_t kTotalAt = 4 + 4 + 10;
    constexpr std::uint64_t kTotalMask = (std::uint64_t{1} << 36) - 1;

    const std::uint64_t frames = decodableFrames(path);
    Header file(path);
    if (!file.isOpen()) {
        return std::unexpected(RepairError::NotReadable);
    }
    std::array<unsigned char, 1> type{};
    if (!file.read(4, type) || (type[0] & 0x7FU) != 0) {
        return std::unexpected(RepairError::NotRecognised);
    }
    const std::uint64_t packed = file.big(kTotalAt, 8);
    RepairResult result;
    result.frames = frames;
    result.changed = (packed & kTotalMask) != (frames & kTotalMask);
    if (result.changed) {
        const std::uint64_t updated = (packed & ~kTotalMask) | (frames & kTotalMask);
        if (!file.putBig(kTotalAt, updated, 8) || !file.flush()) {
            return std::unexpected(RepairError::NotWritable);
        }
    }
    return result;
}

[[nodiscard]] std::optional<pid_t> processOf(const std::filesystem::path &entry)
{
    const std::string name = entry.filename().string();
    pid_t pid = 0;
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), pid);
    if (error != std::errc{} || end == name.data()) {
        return std::nullopt;
    }
    return pid;
}

[[nodiscard]] bool isRunning(pid_t pid)
{
    if (pid == ::getpid()) {
        return true;
    }
    return ::kill(pid, 0) == 0 || errno == EPERM;
}

} // namespace

std::string_view repairErrorName(RepairError error) noexcept
{
    switch (error) {
    case RepairError::NotReadable:
        return "NotReadable";
    case RepairError::NotRecognised:
        return "NotRecognised";
    case RepairError::NotWritable:
        return "NotWritable";
    }
    return "NotRecognised";
}

std::expected<RepairResult, RepairError> repairFile(const std::filesystem::path &path)
{
    std::string magic;
    std::string form;
    {
        Header file(path);
        if (!file.isOpen()) {
            return std::unexpected(RepairError::NotReadable);
        }
        magic = file.tag(0);
        form = file.tag(8);
    }
    if ((magic == "RIFF" || magic == "RF64") && form == "WAVE") {
        return repairWav(path);
    }
    if (magic == "FORM" && (form == "AIFF" || form == "AIFC")) {
        return repairAiff(path);
    }
    if (magic == "fLaC") {
        return repairFlac(path);
    }
    const bool mpeg = magic.starts_with("ID3")
        || (magic.size() == 4 && static_cast<unsigned char>(magic[0]) == 0xFFU
            && (static_cast<unsigned char>(magic[1]) & 0xE0U) == 0xE0U);
    // An M4A is written in fragments, each with its own index, so one
    // cut off plays to its last fragment with nothing to repair.
    std::string box;
    {
        Header file(path);
        box = file.tag(4);
    }
    if (magic == "OggS" || mpeg || box == "ftyp") {
        // A stream of pages or frames that ends where it ends: nothing in it
        // says how long it is, so nothing can be wrong.
        return RepairResult{};
    }
    return std::unexpected(RepairError::NotRecognised);
}

RecordingJournal::RecordingJournal(std::filesystem::path folder)
    : m_folder(std::move(folder))
{
}

std::optional<std::filesystem::path> RecordingJournal::add(const std::filesystem::path &recording)
{
    static std::atomic<std::uint64_t> s_sequence{0};
    std::error_code error;
    std::filesystem::create_directories(m_folder, error);
    if (error) {
        return std::nullopt;
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path entry =
        m_folder / std::format("{}-{}-{}.rec", ::getpid(), s_sequence.fetch_add(1), now);
    // **Written under another name and renamed**, so an engine starting
    // beside this one never reads an entry half-written.
    std::filesystem::path partial = entry;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        out << recording.string();
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(partial, error);
            return std::nullopt;
        }
    }
    std::filesystem::rename(partial, entry, error);
    if (error) {
        std::filesystem::remove(partial, error);
        return std::nullopt;
    }
    return entry;
}

void RecordingJournal::remove(const std::filesystem::path &entry)
{
    std::error_code error;
    std::filesystem::remove(entry, error);
}

std::vector<RepairReport> repairOrphans(const std::filesystem::path &folder)
{
    std::vector<RepairReport> reports;
    std::error_code error;
    std::filesystem::directory_iterator entries(folder, error);
    if (error) {
        return reports; // no journal, no orphans
    }
    std::vector<std::filesystem::path> orphaned;
    for (const std::filesystem::directory_entry &entry : entries) {
        if (entry.path().extension() != ".rec") {
            continue;
        }
        const std::optional<pid_t> pid = processOf(entry.path());
        if (pid && isRunning(*pid)) {
            continue; // still being written, by somebody
        }
        orphaned.push_back(entry.path());
    }
    for (const std::filesystem::path &entry : orphaned) {
        std::ifstream in(entry, std::ios::binary);
        std::string recording((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (!recording.empty() && std::filesystem::exists(recording, error)) {
            reports.push_back(RepairReport{recording, repairFile(recording)});
        }
        std::filesystem::remove(entry, error);
    }
    return reports;
}

} // namespace rb::record
