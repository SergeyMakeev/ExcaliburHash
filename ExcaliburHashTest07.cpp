#include "ExcaliburHash.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace Excalibur;

// Test case for the infinite loop bug when hash table contains only tombstones
TEST(ExcaliburHashInfiniteLoopTest, EmplaceIntoTableWithOnlyTombstones)
{
    HashMap<int32_t, int32_t, 1> hashMap;
    hashMap.reserve(16);

    // Let's first understand the exact growth behavior by testing incrementally
    EXPECT_EQ(hashMap.capacity(), 16); // Should start with minimum capacity

    // "poison" hash_map - filling it up with tombstones, but always keep one element alive to prevent internal optimizations
    hashMap.emplace(-1, -100);
    for (int32_t i = 0; i < 256; ++i)
    {
        if (hashMap.getNumTombstones() == hashMap.capacity() - 1)
        {
            break;
        }

        hashMap.emplace(i, i * 10);
        hashMap.erase(i);
    }

    // the hash map is now poisoned... no more free slots left
    if (hashMap.getNumTombstones() == hashMap.capacity() - 1)
    {

        EXPECT_EQ(hashMap.size(), 1);
        EXPECT_EQ(hashMap.capacity(), hashMap.getNumTombstones() + 1);

        // Set up a timeout to catch infinite loops
        std::atomic<bool> completed{false};
        std::atomic<bool> timed_out{false};

        // Run the potentially infinite operation in a separate thread
        std::thread test_thread(
            [&]()
            {
                try
                {
                    // This should trigger the infinite loop bug!
                    auto result = hashMap.emplace(999, 9999);
                    EXPECT_TRUE(result.second); // Should be inserted
                    EXPECT_EQ(result.first.value(), 9999);
                    completed = true;
                }
                catch (...)
                {
                    completed = true;
                }
            });

        // Wait for a reasonable time - if it takes longer than 2 seconds,
        // we likely have an infinite loop
        auto start_time = std::chrono::steady_clock::now();
        while (!completed && std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!completed)
        {
            timed_out = true;
            test_thread.detach();
            FAIL() << "Infinite loop detected! emplace() did not complete within 2 seconds. ";
        }
        else
        {
            test_thread.join();
            if (!timed_out)
            {
                EXPECT_EQ(hashMap.size(), 1);
                EXPECT_TRUE(hashMap.has(999));
            }
        }
    }
    else
    {
        SUCCEED() << "Can't 'poision' hash map";
    }
}
