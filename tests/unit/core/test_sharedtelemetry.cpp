// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A session's telemetry in shared memory (ADR-0018): the memory the
// engine publishes into and a window maps read-only, and what a window
// refuses to read. Across a real bus, with a daemon, in tests/bus.

#include "core/mockengine.h"
#include "core/sharedtelemetry.h"
#include "core/telemetry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] TelemetrySnapshot snapshotOf(std::uint64_t frame, float peak)
{
    TelemetrySnapshot snapshot;
    snapshot.frame = frame;
    snapshot.running = true;
    snapshot.blockCount = 1;
    std::strncpy(snapshot.blocks[0].id, "volume", kMaxTelemetryIdBytes - 1);
    snapshot.blocks[0].channelCount = 2;
    snapshot.blocks[0].peak[0] = peak;
    snapshot.blocks[0].peak[1] = peak / 2.0f;
    return snapshot;
}

/// How many descriptors this process has open.
[[nodiscard]] int openDescriptors()
{
    return static_cast<int>(QDir(u"/proc/self/fd"_s).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size());
}

/// The lines of /proc/self/maps that are telemetry memory.
[[nodiscard]] QStringList telemetryMappings()
{
    QFile maps(u"/proc/self/maps"_s);
    if (!maps.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList found;
    for (const QByteArray &line : maps.readAll().split('\n')) {
        if (line.contains("reverblabr-telemetry")) {
            found.append(QString::fromLatin1(line));
        }
    }
    return found;
}

/// A sealed memfd holding \a region as it is, whatever it says.
[[nodiscard]] int
memoryHolding(const TelemetryRegion &region, std::size_t size = sizeof(TelemetryRegion), bool seal = true)
{
    const int fd = ::memfd_create("reverblabr-telemetry-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        return -1;
    }
    const std::size_t bytes = std::min(size, sizeof(TelemetryRegion));
    if (::pwrite(fd, &region, bytes, 0) != static_cast<ssize_t>(bytes)) {
        return -1;
    }
    if (seal && ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) != 0) {
        return -1;
    }
    return fd;
}

} // namespace

TEST(SharedTelemetry, WhatIsPublishedIsReadThroughTheSharedDescriptor)
{
    auto engine = SharedTelemetry::create();
    ASSERT_TRUE(engine.has_value());
    ASSERT_NE(engine->writableSlot(), nullptr);
    engine->writableSlot()->store(snapshotOf(7, 0.5f));

    auto window = SharedTelemetry::map(engine->shareReadOnly());
    ASSERT_TRUE(window.has_value());
    const TelemetrySnapshot read = window->slot().load();
    EXPECT_EQ(read.frame, 7U);
    EXPECT_STREQ(read.blocks[0].id, "volume");
    EXPECT_FLOAT_EQ(read.blocks[0].peak[0], 0.5f);
    EXPECT_FLOAT_EQ(read.blocks[0].peak[1], 0.25f);

    // **Later snapshots arrive by themselves**: nothing is asked again.
    engine->writableSlot()->store(snapshotOf(8, 0.75f));
    EXPECT_EQ(window->slot().load().frame, 8U);
    EXPECT_FLOAT_EQ(window->slot().load().blocks[0].peak[0], 0.75f);
}

TEST(SharedTelemetry, AWindowCanNeitherWriteTheMemoryNorBeHandedAWayTo)
{
    auto engine = SharedTelemetry::create();
    ASSERT_TRUE(engine.has_value());

    // The descriptor a window is handed is opened read-only: mapping it
    // for writing is refused by the kernel, whatever the window tries.
    const int shared = engine->shareReadOnly();
    ASSERT_GE(shared, 0);
    EXPECT_EQ(::fcntl(shared, F_GETFL) & O_ACCMODE, O_RDONLY);
    void *writable = ::mmap(nullptr, sizeof(TelemetryRegion), PROT_READ | PROT_WRITE, MAP_SHARED, shared, 0);
    EXPECT_EQ(writable, MAP_FAILED) << "a window could have written the engine's telemetry";
    if (writable != MAP_FAILED) {
        ::munmap(writable, sizeof(TelemetryRegion));
    }

    // And the window's own mapping is read-only and has no slot to write.
    auto window = SharedTelemetry::map(shared);
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(window->writableSlot(), nullptr);
    EXPECT_EQ(window->shareReadOnly(), -1);
    const QStringList mappings = telemetryMappings();
    const auto readOnly =
        std::ranges::count_if(mappings, [](const QString &line) { return line.contains(u" r--s "_s); });
    EXPECT_EQ(readOnly, 1) << mappings.join(u'\n').toStdString();
}

TEST(SharedTelemetry, MemoryOfAnotherLayoutIsNotRead)
{
    const auto refused = [](TelemetryRegion &region) {
        const int fd = memoryHolding(region);
        EXPECT_GE(fd, 0);
        const auto mapped = SharedTelemetry::map(fd);
        return !mapped && mapped.error() == SharedTelemetryError::WrongLayout;
    };
    auto *region = new (::operator new(sizeof(TelemetryRegion), std::align_val_t{64})) TelemetryRegion;
    region->version = kTelemetryVersion + 1;
    EXPECT_TRUE(refused(*region)) << "another version";
    region->version = kTelemetryVersion;
    region->layout = telemetryLayout() ^ 1U;
    EXPECT_TRUE(refused(*region)) << "another layout under the same version";
    region->layout = telemetryLayout();
    region->magic = 0;
    EXPECT_TRUE(refused(*region)) << "not telemetry at all";
    region->magic = kTelemetryMagic;
    const int fd = memoryHolding(*region);
    EXPECT_TRUE(SharedTelemetry::map(fd).has_value()) << "the same memory, as it should be";
    region->~TelemetryRegion();
    ::operator delete(region, std::align_val_t{64});
}

TEST(SharedTelemetry, AnEarlierLayoutsMemoryIsNotRead)
{
    // The band values and then the 64-band block grew the snapshot.
    // **A window from this build handed memory an engine of an earlier
    // layout wrote refuses it**, rather than reading its blocks where the
    // band values now are. Each earlier layout as its own build worked it
    // out: its version, its fingerprint and its size.
    struct Earlier {
        std::uint32_t version;
        std::uint64_t layout;
        std::size_t bytes;
    };
    const std::array<Earlier, 2> earlier{{
        {1, 0x7afda2ad6c511ce9ULL, 8768}, // levels, values and one spectrum
        {2, 0x42fac5df59932e93ULL, 9408}, // 64 band values, the spectrum before
    }};

    auto *region = new (::operator new(sizeof(TelemetryRegion), std::align_val_t{64})) TelemetryRegion;
    const auto refusal = [&](std::size_t size) {
        const int fd = memoryHolding(*region, size);
        EXPECT_GE(fd, 0);
        const auto mapped = SharedTelemetry::map(fd);
        return mapped ? std::optional<SharedTelemetryError>{} : std::optional(mapped.error());
    };
    for (const Earlier &layout : earlier) {
        region->version = layout.version;
        region->layout = layout.layout;
        // Its own size, which is shorter than this build's.
        EXPECT_EQ(refusal(layout.bytes), SharedTelemetryError::NotMapped) << "version " << layout.version;
        // And its header in memory of this build's size: the header is
        // what says no.
        EXPECT_EQ(refusal(sizeof(TelemetryRegion)), SharedTelemetryError::WrongLayout)
            << "version " << layout.version << "'s header";
        // The fingerprint says no by itself, for a build that forgot the
        // version.
        region->version = kTelemetryVersion;
        EXPECT_EQ(refusal(sizeof(TelemetryRegion)), SharedTelemetryError::WrongLayout)
            << "version " << layout.version << "'s fingerprint";
        EXPECT_NE(telemetryLayout(), layout.layout);
    }
    region->~TelemetryRegion();
    ::operator delete(region, std::align_val_t{64});
}

TEST(SharedTelemetry, MemoryThatCouldShrinkOrIsShortIsNotRead)
{
    auto *region = new (::operator new(sizeof(TelemetryRegion), std::align_val_t{64})) TelemetryRegion;

    // **Unsealed, it could be cut short under the mapping**, and reading
    // past the end of a file is a SIGBUS, not an error.
    const auto unsealed = SharedTelemetry::map(memoryHolding(*region, sizeof(TelemetryRegion), false));
    ASSERT_FALSE(unsealed.has_value());
    EXPECT_EQ(unsealed.error(), SharedTelemetryError::NotSealed);

    const auto shorter = SharedTelemetry::map(memoryHolding(*region, 64));
    ASSERT_FALSE(shorter.has_value());
    EXPECT_EQ(shorter.error(), SharedTelemetryError::NotMapped);

    const auto nothing = SharedTelemetry::map(-1);
    ASSERT_FALSE(nothing.has_value());
    EXPECT_EQ(nothing.error(), SharedTelemetryError::NotMapped);

    region->~TelemetryRegion();
    ::operator delete(region, std::align_val_t{64});
}

TEST(SharedTelemetry, NothingIsLeftOpenOrMappedAfterwards)
{
    const int descriptors = openDescriptors();
    const qsizetype mappings = telemetryMappings().size();
    for (int round = 0; round < 50; ++round) {
        auto engine = SharedTelemetry::create();
        ASSERT_TRUE(engine.has_value());
        auto window = SharedTelemetry::map(engine->shareReadOnly());
        ASSERT_TRUE(window.has_value());
        auto moved = std::move(*window);
        EXPECT_EQ(moved.slot().load().frame, 0U);
    }
    EXPECT_EQ(openDescriptors(), descriptors);
    EXPECT_EQ(telemetryMappings().size(), mappings);
}

TEST(SharedTelemetry, TheSamplerPublishesIntoTheSharedMemory)
{
    TelemetrySampler sampler;
    auto window = SharedTelemetry::map(sampler.shareReadOnly());
    ASSERT_TRUE(window.has_value());

    sampler.sample();
    sampler.sample();
    EXPECT_EQ(window->slot().load().frame, 2U);
    EXPECT_EQ(window->slot().publishCount(), sampler.slot().publishCount());
}

TEST(SharedTelemetry, TheMockSharesEachOpenSessionsSlotAndNoOther)
{
    MockEngine mock;
    QSignalSpy opened(&mock, &EngineFacade::sessionOpened);
    mock.createSession(u"Shared"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString sessionId = opened.at(0).at(0).toString();

    EXPECT_EQ(mock.shareTelemetry(u"nobody"_s), -1);
    auto window = SharedTelemetry::map(mock.shareTelemetry(sessionId));
    ASSERT_TRUE(window.has_value());
    // The very slot the mock hands a window in this process.
    EXPECT_EQ(window->slot().publishCount(), mock.telemetryOf(sessionId)->publishCount());
}
