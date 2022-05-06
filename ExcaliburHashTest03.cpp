#include "ExcaliburHash.h"
#include "gtest/gtest.h"

struct CustomStruct
{
    int v = 0;
};

namespace Excalibur
{
template <> struct KeyInfo<CustomStruct>
{
    static inline bool isValid(const CustomStruct& key) noexcept { return key.v < 0x7ffffffe; }
    static inline CustomStruct getTombstone() noexcept { return CustomStruct{0x7fffffff}; }
    static inline CustomStruct getEmpty() noexcept { return CustomStruct{0x7ffffffe}; }
    static inline uint64_t hash(const CustomStruct& /*key*/) noexcept
    {
        // Note: this is a very bad hash function causing 100% collisions
        // added intentionally for the test
        return 3;
    }
    static inline bool isEqual(const CustomStruct& lhs, const CustomStruct& rhs) noexcept { return lhs.v == rhs.v; }
};
} // namespace Excalibur

TEST(SmFlatHashMap, BadHashFunction)
{
    Excalibur::HashTable<CustomStruct, int> ht;
    EXPECT_TRUE(ht.empty());
    EXPECT_EQ(ht.size(), 0u);
    EXPECT_GE(ht.capacity(), 0u);

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

TEST(SmFlatHashMap, EmplaceEdgeCase)
{
    Excalibur::HashTable<CustomStruct, int> ht;
    EXPECT_TRUE(ht.empty());
    EXPECT_EQ(ht.size(), 0u);
    EXPECT_GE(ht.capacity(), 0u);

    const int kNumElements = 40;
    for (int i = 0; i < kNumElements; i++)
    {
        int v = 3 + i;
        auto it = ht.emplace(CustomStruct{256 * i + 1}, v);
        EXPECT_TRUE(it.second);
    }

    // erase half of elements
    for (int i = 0; i < (kNumElements / 2); i++)
    {
        ht.erase(CustomStruct{256 * i + 1});
    }

    // emplace again
    for (int i = 0; i < kNumElements; i++)
    {
        int v = 3 + i;
        auto it = ht.emplace(CustomStruct{256 * i + 1}, v);
        EXPECT_EQ(it.first.value(), v);
    }
}

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

TEST(SmFlatHashMap, CopyTest)
{
    Excalibur::HashTable<int, int> ht1;
    ht1.emplace(1, 2);
    EXPECT_EQ(ht1.size(), uint32_t(1));
    EXPECT_NE(ht1.find(1), ht1.iend());

    Excalibur::HashTable<int, int> ht2;
    EXPECT_NE(ht2.size(), uint32_t(1));
    EXPECT_EQ(ht2.find(1), ht2.iend());

    ht2 = ht1;

    EXPECT_EQ(ht2.size(), uint32_t(1));
    EXPECT_NE(ht2.find(1), ht2.iend());

    EXPECT_EQ(ht1.size(), uint32_t(1));
    EXPECT_NE(ht1.find(1), ht1.iend());

    Excalibur::HashTable<int, int> ht3(ht1);

    EXPECT_EQ(ht3.size(), uint32_t(1));
    EXPECT_NE(ht3.find(1), ht3.iend());

    EXPECT_EQ(ht1.size(), uint32_t(1));
    EXPECT_NE(ht1.find(1), ht1.iend());


    Excalibur::HashTable<int, nullptr_t> ht4;
    ht4.emplace(1);
    ht4.emplace(2);
    EXPECT_EQ(ht4.size(), uint32_t(2));
    EXPECT_TRUE(ht4.has(1));
    EXPECT_TRUE(ht4.has(2));
    EXPECT_FALSE(ht4.has(3));

    Excalibur::HashTable<int, nullptr_t> ht5;
    ht5 = ht4;
    EXPECT_EQ(ht5.size(), uint32_t(2));
    EXPECT_TRUE(ht5.has(1));
    EXPECT_TRUE(ht5.has(2));
    EXPECT_FALSE(ht5.has(3));
}

TEST(SmFlatHashMap, CopyEdgeCases)
{
    Excalibur::HashTable<int, int> ht1;
    ht1.emplace(1, -1);
    ht1.emplace(2, -2);
    ht1.emplace(3, -3);
    EXPECT_EQ(ht1.size(), uint32_t(3));
    EXPECT_NE(ht1.find(1), ht1.iend());
    EXPECT_NE(ht1.find(2), ht1.iend());
    EXPECT_NE(ht1.find(3), ht1.iend());

    // assign to self
    ht1 = ht1;
    EXPECT_EQ(ht1.size(), uint32_t(3));
    EXPECT_NE(ht1.find(1), ht1.iend());
    EXPECT_NE(ht1.find(2), ht1.iend());
    EXPECT_NE(ht1.find(3), ht1.iend());

    Excalibur::HashTable<int, int> ht2;
    EXPECT_TRUE(ht2.empty());
    // assign empty
    ht1 = ht2;
    EXPECT_TRUE(ht1.empty());
}

TEST(SmFlatHashMap, MoveTest)
{
    Excalibur::HashTable<int, int> ht1;
    ht1.emplace(1, 2);
    EXPECT_EQ(ht1.size(), uint32_t(1));
    EXPECT_NE(ht1.find(1), ht1.iend());

    Excalibur::HashTable<int, int> ht2;
    EXPECT_NE(ht2.size(), uint32_t(1));
    EXPECT_EQ(ht2.find(1), ht2.iend());

    ht2 = std::move(ht1);

    EXPECT_EQ(ht2.size(), uint32_t(1));
    EXPECT_NE(ht2.find(1), ht2.iend());

    Excalibur::HashTable<int, int> ht3(std::move(ht2));

    EXPECT_EQ(ht3.size(), uint32_t(1));
    EXPECT_NE(ht3.find(1), ht3.iend());
}

static const int kNumElementsInTinyTable = 1;
static Excalibur::HashTable<int, int> makeTinyHashTable(int valueOffset = 0)
{
    Excalibur::HashTable<int, int> ht;
    EXPECT_TRUE(ht.empty());
    for (int i = 0; i < kNumElementsInTinyTable; i++)
    {
        ht.emplace(i, i + valueOffset);
    }
    EXPECT_EQ(ht.size(), uint32_t(kNumElementsInTinyTable));
    return ht;
}

static const int kNumElementsInHugeTable = 1000;
static Excalibur::HashTable<int, int> makeHugeHashTable()
{
    Excalibur::HashTable<int, int> ht;
    EXPECT_TRUE(ht.empty());
    for (int i = 0; i < kNumElementsInHugeTable; i++)
    {
        ht.emplace(i, i);
    }
    EXPECT_EQ(ht.size(), uint32_t(kNumElementsInHugeTable));
    return ht;
}

static Excalibur::HashTable<int, int> makeHashTable(int numElements)
{
    Excalibur::HashTable<int, int> ht;
    EXPECT_TRUE(ht.empty());
    for (int i = 0; i < numElements; i++)
    {
        ht.emplace(i, i);
    }
    EXPECT_EQ(ht.size(), uint32_t(numElements));
    return ht;
}

TEST(SmFlatHashMap, MoveEdgeCases)
{
    {
        // many to one
        Excalibur::HashTable<int, int> htSmall = makeTinyHashTable();
        Excalibur::HashTable<int, int> htHuge = makeHugeHashTable();
        htSmall = std::move(htHuge);
        EXPECT_EQ(htSmall.size(), uint32_t(kNumElementsInHugeTable));
    }

    {
        // one to many
        Excalibur::HashTable<int, int> htSmall = makeTinyHashTable();
        Excalibur::HashTable<int, int> htHuge = makeHugeHashTable();
        htHuge = std::move(htSmall);
        EXPECT_EQ(htHuge.size(), uint32_t(kNumElementsInTinyTable));
    }

    {
        // many to many
        Excalibur::HashTable<int, int> htHuge1 = makeHugeHashTable();
        Excalibur::HashTable<int, int> htHuge2 = makeHashTable((kNumElementsInHugeTable - 4));
        htHuge1 = std::move(htHuge2);
        EXPECT_EQ(htHuge1.size(), uint32_t((kNumElementsInHugeTable - 4)));
    }

    {
        // one to one
        const int kValueOffset = 13;
        Excalibur::HashTable<int, int> htSmall1 = makeTinyHashTable();
        Excalibur::HashTable<int, int> htSmall2 = makeTinyHashTable(kValueOffset);
        htSmall1 = std::move(htSmall2);
        EXPECT_EQ(htSmall1.size(), uint32_t(kNumElementsInTinyTable));
        auto it = htSmall1.ibegin();
        EXPECT_EQ(it.value(), kValueOffset);
    }

    {
        // zero to one
        Excalibur::HashTable<int, int> htSmall = makeTinyHashTable();
        Excalibur::HashTable<int, int> htEmpty;
        htSmall = std::move(htEmpty);
        EXPECT_EQ(htSmall.size(), uint32_t(0));
    }

    {
        // zero to many
        Excalibur::HashTable<int, int> htHuge = makeHugeHashTable();
        Excalibur::HashTable<int, int> htEmpty;
        htHuge = std::move(htEmpty);
        EXPECT_EQ(htHuge.size(), uint32_t(0));
    }

    {
        // move to self
        Excalibur::HashTable<int, int> htHuge = makeHugeHashTable();
        htHuge = std::move(htHuge);
        EXPECT_EQ(htHuge.size(), uint32_t(kNumElementsInHugeTable));
    }
}

TEST(SmFlatHashMap, TryToEmplaceDuplicate)
{
    // note: CustomStruct intentionally has a very bad hash function that always returns 3
    Excalibur::HashTable<CustomStruct, CustomStruct> ht;

    // note i0 and i1 produces collision due to weak hash function
    CustomStruct& i0 = ht[CustomStruct{0}];
    EXPECT_EQ(i0.v, 0);
    i0.v++;

    CustomStruct& i1 = ht[CustomStruct{1}];
    EXPECT_EQ(i1.v, 0);
    i1.v++;

    // erase i0
    ht.erase(CustomStruct{0});

    // check if we found the same i1 or not
    CustomStruct& _i1 = ht[CustomStruct{1}];
    EXPECT_EQ(i1.v, 1);
    EXPECT_EQ(_i1.v, 1);

    EXPECT_EQ(&_i1.v, &i1.v);
}

TEST(SmFlatHashMap, ReserveTest)
{
    Excalibur::HashTable<int, int> ht;
    EXPECT_TRUE(ht.empty());
    EXPECT_GE(ht.capacity(), 0u);

    ht.reserve(31);
    EXPECT_TRUE(ht.empty());
    EXPECT_EQ(ht.capacity(), 32u);

    ht.reserve(32);
    EXPECT_TRUE(ht.empty());
    EXPECT_EQ(ht.capacity(), 32u);

    ht.emplace(1, -1);
    ht.emplace(9, -9);
    EXPECT_EQ(ht.size(), 2u);
    EXPECT_EQ(ht.capacity(), 32u);

    ht.reserve(128);
    EXPECT_EQ(ht.size(), 2u);
    EXPECT_EQ(ht.capacity(), 128u);
    EXPECT_TRUE(ht.has(1));
    EXPECT_TRUE(ht.has(9));

    ht.reserve(55);
    EXPECT_EQ(ht.size(), 2u);
    EXPECT_EQ(ht.capacity(), 128u);
    EXPECT_TRUE(ht.has(1));
    EXPECT_TRUE(ht.has(9));
}