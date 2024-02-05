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

static int ctorCallCount = 0;
static int dtorCallCount = 0;
static int assignCallCount = 0;

struct ComplexVal
{
    ComplexVal() = delete;
    ComplexVal(const ComplexVal& other) = delete;
    ComplexVal& operator=(const ComplexVal& other) noexcept = delete;

    ComplexVal(uint32_t _v) noexcept
        : v(_v)
        , status(Status::Constructed)
    {
        ctorCallCount++;
    }

    ComplexVal(ComplexVal&& other) noexcept
        : v(other.v)
        , status(Status::MoveConstructed)
    {
        EXPECT_NE(other.status, Status::Destructed);
        ctorCallCount++;
        other.status = Status::Moved;
    }

    ~ComplexVal() noexcept
    {
        EXPECT_NE(status, Status::Destructed);
        status = Status::Destructed;
        dtorCallCount++;
    }

    ComplexVal& operator=(ComplexVal&& other) noexcept
    {
        status = Status::MoveAssigned;
        other.status = Status::Moved;

        v = other.v;
        assignCallCount++;
        return *this;
    }

    uint32_t v;
    enum class Status
    {
        Constructed = 0,
        MoveConstructed = 1,
        MoveAssigned = 2,
        Destructed = 3,
        Moved = 4,
    };
    Status status;
};

template <typename TFrom, typename TTo> void doMoveAssignmentTest(TFrom& hm1, TTo& hm2, size_t numValuesToInsert)
{
    // insert values
    for (size_t i = 0; i < numValuesToInsert; i++)
    {
        hm1.emplace(int(i), uint32_t(i + 13));
    }

    {
        EXPECT_TRUE(hm1.has(0));
        EXPECT_TRUE(hm1.has(1));
        EXPECT_TRUE(hm1.has(2));

        auto it1 = hm1.find(0);
        auto it2 = hm1.find(1);
        auto it3 = hm1.find(2);

        EXPECT_NE(it1, hm1.end());
        EXPECT_NE(it2, hm1.end());
        EXPECT_NE(it3, hm1.end());

        const int& key1 = it1->first;
        const ComplexVal& value1 = it1->second;

        const int& key2 = it2->first;
        const ComplexVal& value2 = it2->second;

        const int& key3 = it3->first;
        const ComplexVal& value3 = it3->second;

        EXPECT_EQ(key1, 0);
        EXPECT_EQ(key2, 1);
        EXPECT_EQ(key3, 2);

        EXPECT_EQ(value1.v, uint32_t(13));
        EXPECT_EQ(value2.v, uint32_t(14));
        EXPECT_EQ(value3.v, uint32_t(15));
    }

    // move assign to other hash map
    hm2 = std::move(hm1);

    {
        EXPECT_TRUE(hm2.has(0));
        EXPECT_TRUE(hm2.has(1));
        EXPECT_TRUE(hm2.has(2));

        auto it1 = hm2.find(0);
        auto it2 = hm2.find(1);
        auto it3 = hm2.find(2);

        EXPECT_NE(it1, hm2.end());
        EXPECT_NE(it2, hm2.end());
        EXPECT_NE(it3, hm2.end());

        const int& key1 = it1->first;
        const ComplexVal& value1 = it1->second;

        const int& key2 = it2->first;
        const ComplexVal& value2 = it2->second;

        const int& key3 = it3->first;
        const ComplexVal& value3 = it3->second;

        EXPECT_EQ(key1, 0);
        EXPECT_EQ(key2, 1);
        EXPECT_EQ(key3, 2);

        EXPECT_EQ(value1.v, uint32_t(13));
        EXPECT_EQ(value2.v, uint32_t(14));
        EXPECT_EQ(value3.v, uint32_t(15));
    }
}

TEST(SmFlatHashMap, InlineStorageTest02)
{
    {
        // move inline -> inline
        ctorCallCount = 0;
        dtorCallCount = 0;
        assignCallCount = 0;
        {
            Excalibur::HashMap<int, ComplexVal, 8> hm1;
            Excalibur::HashMap<int, ComplexVal, 8> hm2;

            doMoveAssignmentTest(hm1, hm2, 3);
        }
        EXPECT_EQ(ctorCallCount, dtorCallCount);
    }

    {
        // move heap -> heap
        ctorCallCount = 0;
        dtorCallCount = 0;
        assignCallCount = 0;
        {
            Excalibur::HashMap<int, ComplexVal, 1> hm1;
            Excalibur::HashMap<int, ComplexVal, 1> hm2;

            doMoveAssignmentTest(hm1, hm2, 100);
        }
        EXPECT_EQ(ctorCallCount, dtorCallCount);
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
