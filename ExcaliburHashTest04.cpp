#include "ExcaliburHash.h"
#include "gtest/gtest.h"
#include <array>

struct ComplexValue
{
    ComplexValue(const ComplexValue& other)
        : isMoved(other.isMoved)
        , isDeleted(other.isDeleted)
    {
        EXPECT_FALSE(other.isMoved);
        EXPECT_FALSE(other.isDeleted);
    }

    ComplexValue& operator=(const ComplexValue& other) noexcept
    {
        EXPECT_FALSE(other.isMoved);
        EXPECT_FALSE(other.isDeleted);
        isMoved = other.isMoved;
        isDeleted = other.isDeleted;
    }

    ComplexValue() noexcept
        : isMoved(false)
        , isDeleted(false)
    {
    }

    ComplexValue(ComplexValue&& other) noexcept
        : isMoved(other.isMoved)
        , isDeleted(other.isDeleted)
    {
        EXPECT_FALSE(other.isDeleted);
        other.isMoved = true;
    }

    ~ComplexValue() noexcept { isDeleted = true; }

    ComplexValue& operator=(ComplexValue&& other) noexcept
    {
        isDeleted = other.isDeleted;
        isMoved = other.isMoved;
        other.isMoved = true;
        return *this;
    }

    bool isMoved;
    bool isDeleted;
};

TEST(SmFlatHashMap, InsertFromItselfWhileGrow)
{
    for (int i = 1; i <= 1000; i++)
    {
        // printf("Step %d\n", i);

        // create hash map and insert one element
        Excalibur::HashTable<int, ComplexValue> ht;

        // insert some elements into the hash maps
        for (int j = 0; j < i; j++)
        {
            ht.emplace(j, ComplexValue{});
        }

        // find the first inserted element (get a valid iterator)
        auto it = ht.find(0);
        ASSERT_NE(it, ht.end());

        // insert a new element using the above iterator
        // (a hash map can grow during insertion and this could invalidate the iterator)
        ht.emplace(-1, it->second);

        auto it2 = ht.find(-1);
        ASSERT_NE(it2, ht.end());
    }
}
