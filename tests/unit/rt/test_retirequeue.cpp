// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/retirequeue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

using namespace rb::rt;

namespace
{

/// Records the thread its destructor ran on, which is the whole question.
struct Instrumented {
    static std::atomic<std::size_t> s_destroyed;
    static std::atomic<std::thread::id> s_lastDestroyingThread;

    int payload = 0;

    explicit Instrumented(int value)
        : payload(value)
    {
    }

    ~Instrumented()
    {
        s_lastDestroyingThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
        s_destroyed.fetch_add(1, std::memory_order_relaxed);
    }
};

std::atomic<std::size_t> Instrumented::s_destroyed{0};
std::atomic<std::thread::id> Instrumented::s_lastDestroyingThread{};

/// A different type, to prove the queue keeps hold of what each object is.
struct OtherInstrumented {
    static std::atomic<std::size_t> s_destroyed;
    ~OtherInstrumented()
    {
        s_destroyed.fetch_add(1, std::memory_order_relaxed);
    }
};

std::atomic<std::size_t> OtherInstrumented::s_destroyed{0};

} // namespace

TEST(RetireQueue, FreesWhatItIsGiven)
{
    Instrumented::s_destroyed.store(0, std::memory_order_relaxed);
    RetireQueue queue(8);

    auto object = std::make_unique<Instrumented>(3);
    ASSERT_TRUE(queue.tryRetire(object));
    EXPECT_EQ(object, nullptr) << "handing an object over gives up ownership of it";
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 0u) << "nothing is freed at the handover";
    EXPECT_EQ(queue.pending(), 1u);

    EXPECT_EQ(queue.drain(), 1u);
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(queue.pending(), 0u);
}

TEST(RetireQueue, TakesAnObjectHeldByAPlainPointer)
{
    Instrumented::s_destroyed.store(0, std::memory_order_relaxed);
    RetireQueue queue(1);

    auto *object = new Instrumented(4);
    ASSERT_TRUE(queue.tryRetire(object));
    EXPECT_EQ(object, nullptr) << "a pointer handed over is emptied";
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 0u);

    // Full: refused, and still the caller's.
    auto *another = new Instrumented(5);
    EXPECT_FALSE(queue.tryRetire(another));
    EXPECT_NE(another, nullptr);
    EXPECT_EQ(queue.drain(), 1u);
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 1u);
    ASSERT_TRUE(queue.tryRetire(another));
    EXPECT_EQ(queue.drain(), 1u);
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 2u);
}

TEST(RetireQueue, FreesEachObjectAsItsOwnType)
{
    Instrumented::s_destroyed.store(0, std::memory_order_relaxed);
    OtherInstrumented::s_destroyed.store(0, std::memory_order_relaxed);
    RetireQueue queue(8);

    auto first = std::make_unique<Instrumented>(1);
    auto second = std::make_unique<OtherInstrumented>();
    ASSERT_TRUE(queue.tryRetire(first));
    ASSERT_TRUE(queue.tryRetire(second));

    EXPECT_EQ(queue.drain(), 2u);
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(OtherInstrumented::s_destroyed.load(std::memory_order_relaxed), 1u);
}

TEST(RetireQueue, FreesOnTheDrainingThreadAndNowhereElse)
{
    // This is the point of the whole class: the audio thread must not free
    // memory, because the allocator takes a lock.
    Instrumented::s_destroyed.store(0, std::memory_order_relaxed);
    RetireQueue queue(256);

    std::thread::id audioThreadId;
    std::thread audioThread([&] {
        audioThreadId = std::this_thread::get_id();
        for (int value = 0; value < 200; ++value) {
            auto object = std::make_unique<Instrumented>(value);
            ASSERT_TRUE(queue.tryRetire(object));
        }
    });
    audioThread.join();

    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 0u)
        << "nothing may be freed while the audio thread owns it";

    EXPECT_EQ(queue.drain(), 200u);
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 200u);
    EXPECT_EQ(Instrumented::s_lastDestroyingThread.load(std::memory_order_relaxed), std::this_thread::get_id());
    EXPECT_NE(Instrumented::s_lastDestroyingThread.load(std::memory_order_relaxed), audioThreadId);
}

TEST(RetireQueue, AFullQueueLeavesTheCallerHoldingTheObject)
{
    // Refusing has to mean "you keep it": the alternative is the audio thread
    // freeing the object itself, which is the one thing it must not do.
    Instrumented::s_destroyed.store(0, std::memory_order_relaxed);
    RetireQueue queue(1);

    auto first = std::make_unique<Instrumented>(1);
    ASSERT_TRUE(queue.tryRetire(first));

    auto second = std::make_unique<Instrumented>(2);
    const Instrumented *raw = second.get();
    EXPECT_FALSE(queue.tryRetire(second));
    EXPECT_EQ(second.get(), raw) << "a refused object is still the caller's";
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(queue.retireRefusedCount(), 1u);

    // The caller offers it again once the queue has been emptied.
    EXPECT_EQ(queue.drain(), 1u);
    EXPECT_TRUE(queue.tryRetire(second));
    EXPECT_EQ(queue.drain(), 1u);
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 2u);
}

TEST(RetireQueue, TakingNothingIsNotAFailure)
{
    RetireQueue queue(1);
    std::unique_ptr<Instrumented> empty;
    EXPECT_TRUE(queue.tryRetire(empty));
    EXPECT_EQ(queue.pending(), 0u);
}

TEST(RetireQueue, FreesWhatIsLeftWhenItGoesAway)
{
    Instrumented::s_destroyed.store(0, std::memory_order_relaxed);
    {
        RetireQueue queue(8);
        auto object = std::make_unique<Instrumented>(9);
        ASSERT_TRUE(queue.tryRetire(object));
        EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 0u);
    }
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), 1u);
}

TEST(RetireQueue, KeepsUpWithAThreadThatRetiresWhileAnotherDrains)
{
    Instrumented::s_destroyed.store(0, std::memory_order_relaxed);
    constexpr int kObjects = 100'000;
    RetireQueue queue(1024);

    std::atomic<bool> producing{true};
    std::thread drainer([&] {
        while (producing.load(std::memory_order_relaxed) || queue.pending() > 0) {
            static_cast<void>(queue.drain());
        }
    });

    // A refused object is held on to and offered again, which is what a
    // caller on the audio thread has to do: letting it go out of scope would
    // free it there.
    int retired = 0;
    std::unique_ptr<Instrumented> object;
    while (retired < kObjects) {
        if (object == nullptr) {
            object = std::make_unique<Instrumented>(retired);
        }
        if (queue.tryRetire(object)) {
            ++retired;
        }
    }
    producing.store(false, std::memory_order_relaxed);
    drainer.join();

    static_cast<void>(queue.drain());
    EXPECT_EQ(Instrumented::s_destroyed.load(std::memory_order_relaxed), static_cast<std::size_t>(kObjects));
    EXPECT_EQ(queue.pending(), 0u);
}
