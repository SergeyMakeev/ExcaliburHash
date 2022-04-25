#include "ExcaliburHash.h"
#include "gtest/gtest.h"

static int ctorCallCount = 0;
static int dtorCallCount = 0;
static int assignCallCount = 0;

struct ComplexStruct
{
    ComplexStruct() = delete;
    ComplexStruct(const ComplexStruct& other) = delete;
    ComplexStruct& operator=(const ComplexStruct& other) noexcept = delete;

    ComplexStruct(uint32_t _v) noexcept
        : v(_v)
    {
        ctorCallCount++;
    }

    ComplexStruct(ComplexStruct&& other) noexcept
        : v(other.v)
    {
        ctorCallCount++;
    }

    ~ComplexStruct() noexcept { dtorCallCount++; }

    ComplexStruct& operator=(ComplexStruct&& other) noexcept
    {
        v = other.v;
        assignCallCount++;
        return *this;
    }

    uint32_t v;
};

namespace Excalibur
{
template <> struct KeyInfo<ComplexStruct>
{
    static inline ComplexStruct getEmpty() noexcept { return ComplexStruct{0xffffffff}; }
    static inline uint64_t hash(const ComplexStruct& key) noexcept { return std::hash<uint32_t>{}(key.v); }
    static inline bool isEqual(const ComplexStruct& lhs, const ComplexStruct& rhs) noexcept { return lhs.v == rhs.v; }
};

} // namespace Excalibur

TEST(SmFlatHashMap, BasicTest33)
{
    ctorCallCount = 0;
    dtorCallCount = 0;
    assignCallCount = 0;

    {
        // empty hash table
        Excalibur::HashTable<ComplexStruct, int> ht;
        EXPECT_TRUE(ht.empty());
        EXPECT_EQ(ht.size(), 0u);
        EXPECT_EQ(ht.capacity(), 0u);

        // emplace elements
        const uint32_t kNumElements = 5;
        for (uint32_t i = 0; i < kNumElements; i++)
        {
            int v = 3 + i;
            auto it = ht.emplace(ComplexStruct{256 * i + 1}, v);
            EXPECT_TRUE(it.second);
        }

        EXPECT_FALSE(ht.empty());
        EXPECT_EQ(ht.size(), kNumElements);
        EXPECT_GE(ht.capacity(), kNumElements);

        // check if those elements exists and have expected value and then remove then
        for (uint32_t i = 0; i < kNumElements; i++)
        {
            uint32_t k = 256 * i + 1;

            auto htVal = ht.find(k);
            ASSERT_NE(htVal, ht.iend());

            int refVal = 3 + i;
            EXPECT_EQ(htVal.value(), refVal);

            bool isErased = ht.erase(k);
            EXPECT_TRUE(isErased);
        }
        EXPECT_TRUE(ht.empty());
        EXPECT_EQ(ht.size(), 0u);
        EXPECT_GE(ht.capacity(), 0u);

        ht.emplace(ComplexStruct{13}, 13);
        ht.emplace(ComplexStruct{6}, 6);
        EXPECT_FALSE(ht.empty());
        EXPECT_EQ(ht.size(), 2u);
        EXPECT_GE(ht.capacity(), 2u);

        ht.clear();
        EXPECT_TRUE(ht.empty());
        EXPECT_EQ(ht.size(), 0u);
        EXPECT_GE(ht.capacity(), 0u);

        ht.emplace(ComplexStruct{13}, 13);
        ht.emplace(ComplexStruct{6}, 6);
        ht.emplace(ComplexStruct{9}, 9);
        ht.emplace(ComplexStruct{15}, 15);
        EXPECT_FALSE(ht.empty());
        EXPECT_EQ(ht.size(), 4u);
        EXPECT_GE(ht.capacity(), 4u);
    }

    EXPECT_EQ(ctorCallCount, dtorCallCount);
}