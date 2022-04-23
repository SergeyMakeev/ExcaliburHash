#include "ExcaliburHash.h"
#include "gtest/gtest.h"

struct CustomStruct
{
    int v;
};

namespace Excalibur
{
template <> struct KeyInfo<CustomStruct>
{
    static inline CustomStruct getEmpty() noexcept { return CustomStruct{2147483647}; }
    static uint64_t hash(const CustomStruct& /*key*/) noexcept
    {
        // Note: this is a very bad hash function
        // added intentionally for the test
        return 3;
    }
    static bool isEqual(const CustomStruct& lhs, const CustomStruct& rhs) noexcept { return lhs.v == rhs.v; }
};

} // namespace Excalibur

TEST(SmFlatHashMap, BadHashFunction)
{
    Excalibur::HashTable<CustomStruct, int> ht;
    EXPECT_TRUE(ht.empty());
    EXPECT_EQ(ht.size(), 0u);
    EXPECT_EQ(ht.capacity(), 0u);

    const int kNumElements = 500;
    for (int i = 0; i < kNumElements; i++)
    {
        int v = 3 + i;
        auto it = ht.emplace(CustomStruct{256 * i + 1}, v);
        EXPECT_TRUE(it.second);
    }

    for (int i = 0; i < kNumElements; i++)
    {
        auto v = ht.find(CustomStruct{256 * i + 1});
        ASSERT_NE(v, ht.iend());
        int refVal = 3 + i;
        EXPECT_EQ(v.value(), refVal);
    }

    // search for non-existing keys
    auto f0 = ht.find(CustomStruct{-3});
    EXPECT_EQ(f0, ht.iend());
    auto f1 = ht.find(CustomStruct{-13});
    EXPECT_EQ(f1, ht.iend());

    // erase non-existing keys
    bool e0 = ht.erase(CustomStruct{-3});
    EXPECT_FALSE(e0);
    bool e1 = ht.erase(CustomStruct{-13});
    EXPECT_FALSE(e1);
}

namespace Excalibur
{
template <> struct KeyInfo<std::string>
{
    static inline std::string getEmpty() noexcept { return std::string(); }
    static uint64_t hash(const std::string& key) noexcept { return std::hash<std::string>{}(key); }
    static bool isEqual(const std::string& lhs, const std::string& rhs) noexcept { return lhs == rhs; }
};
} // namespace Excalibur

TEST(SmFlatHashMap, ComplexStruct)
{
    {

        Excalibur::HashTable<std::string, std::string> ht;

        const char* keyStr = "Hello";

        std::string key = keyStr;

        EXPECT_TRUE(ht.empty());
        ht[key] = "World";
        EXPECT_FALSE(ht.empty());

        // check that key wasn't affected (use after move edge case)
        EXPECT_STREQ(key.c_str(), keyStr);
        EXPECT_STREQ(key.c_str(), "Hello");

        auto it = ht.find(key);
        ASSERT_NE(it, ht.end());

        EXPECT_STREQ(it->second.get().c_str(), "World");

        std::string& val = ht[key];
        EXPECT_STREQ(val.c_str(), "World");

        val = "Test";
        EXPECT_STREQ(it->second.get().c_str(), "Test");

        EXPECT_FALSE(ht.empty());
        ht.erase(it);
        EXPECT_TRUE(ht.empty());

        ht[key] = "World";
        ht.clear();

        // destroy non empty hash table (with non POD key/value type)
        ht[key] = "World";
    }
}

void ConstCorrectnessTest(const Excalibur::HashTable<int, int>& ht)
{
    // this test is more of an API const correctness test (this code is expected to compile without errors)
    uint32_t size = ht.size();
    EXPECT_EQ(size, uint32_t(1));

    uint32_t capacity = ht.capacity();
    EXPECT_GE(capacity, uint32_t(1));

    bool isEmpty = ht.empty();
    EXPECT_FALSE(isEmpty);

    bool shouldBeTrue = ht.has(1);
    EXPECT_TRUE(shouldBeTrue);

    bool shouldBeFalse = ht.has(-1);
    EXPECT_FALSE(shouldBeFalse);

    uint64_t sum = 0;

    auto it1 = ht.find(1);
    ASSERT_NE(it1, ht.iend());
    sum += it1->first;
    sum += it1->second;

    // this should not compile!
    /*
    int& v = it1->second;
    v = 7;
    */

    auto it2 = ht.find(-1);
    EXPECT_EQ(it2, ht.iend());

    for (auto it3 = ht.begin(); it3 != ht.end(); ++it3)
    {
        sum += *it3;
    }

    for (auto it4 = ht.vbegin(); it4 != ht.vend(); ++it4)
    {
        sum += *it4;
    }

    for (auto it5 = ht.ibegin(); it5 != ht.iend(); ++it5)
    {
        sum += it5->first;
        sum += it5->second;

        // this should not compile!
        /*
        int& v = it5->second;
        v = 7;
        */
    }

    for (int k : ht)
    {
        sum += k;
    }

    for (int k : ht.keys())
    {
        sum += k;
    }

    for (int k : ht.values())
    {
        sum += k;
    }

    for (auto& kv : ht.items())
    {
        sum += kv.first;
        sum += kv.second;

        // this should not compile!
        /*
        int& v = kv.second;
        v = 7;
        */
    }
}

void MutabilityTest(Excalibur::HashTable<int, int>& ht)
{
    // find() can be used to get non-const kv iterator
    auto it1 = ht.find(1);
    ASSERT_NE(it1, ht.iend());
    int& v = it1->second;
    EXPECT_EQ(v, 2);
    v = 3;
    EXPECT_EQ(v, 3);

    // you can mutate values when iterate
    for (auto it2 = ht.vbegin(); it2 != ht.vend(); ++it2)
    {
        int& val = *it2;
        val++;
    }

    for (auto it3 = ht.ibegin(); it3 != ht.iend(); ++it3)
    {
        int& val = it3->second;
        val++;
    }

    for (int& k : ht.values())
    {
        k++;
    }

    for (auto& kv : ht.items())
    {
        int& val = kv.second;
        val++;
    }

    auto it4 = ht.find(1);
    ASSERT_NE(it4, ht.iend());
    const int& testVal = it4->second;
    EXPECT_EQ(testVal, 7);
}

TEST(SmFlatHashMap, ConstCorrectness)
{
    Excalibur::HashTable<int, int> ht;
    ht.emplace(1, 2);
    ConstCorrectnessTest(ht);
    MutabilityTest(ht);
}