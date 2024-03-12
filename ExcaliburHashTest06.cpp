#include "ExcaliburHash.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>

TEST(SmFlatHashMap, Rehash)
{
	Excalibur::HashTable<int, int> ht;
    ht.emplace(1, -1);
    ht.erase(1);

    ht.emplace(2, -2);
    ht.emplace(5, -5);
    ht.erase(5);

    ht.emplace(7, -7);
    ht.erase(7);

    ht.emplace(8, -8);
    ht.emplace(9, -9);
    ht.erase(9);

    ht.emplace(10, -10);
    ht.erase(10);

    ht.emplace(11, -11);
    ht.erase(11);

    ht.emplace(12, -12);
    ht.erase(12);

    ht.emplace(13, -13);
    ht.erase(13);

    ht.emplace(14, -14);
    ht.erase(14);

    ht.emplace(15, -15);

    EXPECT_GE(ht.getNumTombstones(), uint32_t(1));

    ht.rehash();

    EXPECT_EQ(ht.getNumTombstones(), uint32_t(0));
}

