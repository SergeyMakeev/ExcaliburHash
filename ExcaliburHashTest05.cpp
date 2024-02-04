#include "ExcaliburHash.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>

namespace Excalibur
{
template <> struct KeyInfo<std::string>
{
    static inline bool isValid(const std::string& key) noexcept { return !key.empty() && key.data()[0] != char(1); }
    static inline std::string getTombstone() noexcept
    {
        // and let's hope that small string optimization will do the job
        return std::string(1, char(1));
    }
    static inline std::string getEmpty() noexcept { return std::string(); }
    static inline uint64_t hash(const std::string& key) noexcept { return std::hash<std::string>{}(key); }
    static inline bool isEqual(const std::string& lhs, const std::string& rhs) noexcept { return lhs == rhs; }
};
} // namespace Excalibur

TEST(SmFlatHashMap, InlineStorageTest01)
{
    // create hash map and insert one element
    Excalibur::HashTable<std::string, std::string, 8> ht;

    EXPECT_GE(ht.capacity(), uint32_t(4));

    auto it1 = ht.emplace(std::string("hello1"), std::string("world1"));
    EXPECT_TRUE(it1.second);
    auto it2 = ht.emplace(std::string("hello2"), std::string("world2"));
    EXPECT_TRUE(it2.second);

    EXPECT_EQ(ht.size(), uint32_t(2));

    {
        auto _it1 = ht.find("hello1");
        ASSERT_NE(_it1, ht.end());
        const std::string& val1 = _it1->second;
        ASSERT_EQ(val1, "world1");

        auto _it2 = ht.find("hello2");
        ASSERT_NE(_it2, ht.end());
        const std::string& val2 = _it2->second;
        ASSERT_EQ(val2, "world2");
    }

    for (int i = 0; i < 1000; i++)
    {
        ht.emplace(std::to_string(i), "tmp");
    }

    {
        auto _it1 = ht.find("hello1");
        ASSERT_NE(_it1, ht.end());
        const std::string& val1 = _it1->second;
        ASSERT_EQ(val1, "world1");

        auto _it2 = ht.find("hello2");
        ASSERT_NE(_it2, ht.end());
        const std::string& val2 = _it2->second;
        ASSERT_EQ(val2, "world2");
    }
}


TEST(SmFlatHashMap, AliasNameTest)
{
    {
        Excalibur::HashMap<int, int> hm;
        auto it1 = hm.emplace(1, 2);
        EXPECT_TRUE(it1.second);
        auto it2 = hm.emplace(2, 3);
        EXPECT_TRUE(it2.second);

        auto _it1 = hm.find(1);
        ASSERT_NE(_it1, hm.end());

        auto _it2 = hm.find(2);
        ASSERT_NE(_it2, hm.end());

        auto _it3 = hm.find(3);
        ASSERT_EQ(_it3, hm.end());

        const int& val1 = _it1->second;
        const int& val2 = _it2->second;
        ASSERT_EQ(val1, 2);
        ASSERT_EQ(val2, 3);
    }

    {
        Excalibur::HashSet<int> hs;
        auto it1 = hs.emplace(1);
        EXPECT_TRUE(it1.second);
        auto it2 = hs.emplace(1);
        EXPECT_FALSE(it2.second);
        auto it3 = hs.emplace(2);
        EXPECT_TRUE(it3.second);

        EXPECT_TRUE(hs.has(1));
        EXPECT_TRUE(hs.has(2));
        EXPECT_FALSE(hs.has(3));
    }






}
