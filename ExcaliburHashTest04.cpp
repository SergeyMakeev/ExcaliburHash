#include "ExcaliburHash.h"
#include "gtest/gtest.h"
#include <array>

struct ComplexValue
{
    ComplexValue(const ComplexValue& other)
        : isMoved(other.isMoved)
    {
        /*
        if (other.isMoved)
        {
            int a = 0;
            a = 7;
        }
        */
        EXPECT_FALSE(other.isMoved);
    }

    ComplexValue& operator=(const ComplexValue& other) noexcept
    {
        /*
        if (other.isMoved)
        {
            int a = 0;
            a = 7;
        }
        */

        EXPECT_FALSE(other.isMoved);
        isMoved = other.isMoved;
    }

    ComplexValue() noexcept
        : isMoved(false)
    {
    }

    ComplexValue(ComplexValue&& other) noexcept
        : isMoved(other.isMoved)
    {
        other.isMoved = true;
    }

    ~ComplexValue() noexcept {}

    ComplexValue& operator=(ComplexValue&& other) noexcept
    {
        isMoved = other.isMoved;
        other.isMoved = true;
        return *this;
    }

    bool isMoved;
};

TEST(SmFlatHashMap, InsertFromItselfWhileGrow)
{
    for (int i = 1; i <= 1000; i++)
    {
        //printf("Step %d\n", i);

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
