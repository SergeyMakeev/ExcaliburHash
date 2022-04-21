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