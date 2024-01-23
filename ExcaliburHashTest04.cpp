#include "ExcaliburHash.h"
#include "gtest/gtest.h"
#include <array>

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
        return *this;
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

TEST(SmFlatHashMap, CopyableIterators)
{
    Excalibur::HashTable<std::string, std::string> ht;

    for (int i = 1; i < 16; i++)
    {
        ht.emplace(std::to_string(i), std::to_string(i+1));
    }
    
    Excalibur::HashTable<std::string, std::string>::IteratorKV it = ht.find(std::to_string(1));
    ASSERT_NE(it, ht.end());
    const std::string& val = it->second;

    Excalibur::HashTable<std::string, std::string>::IteratorKV it2 = it;
    ASSERT_NE(it2, ht.end());
    const std::string& val2 = it2->second;

    EXPECT_EQ(it, it2);
    EXPECT_EQ(val, val2);

    ++it2;
    EXPECT_NE(it, it2);

    it2 = it;
    EXPECT_EQ(it, it2);

/*
    printf("pass1\n");
    for (Excalibur::HashTable<std::string, std::string>::IteratorKV it3 = ht.ibegin(); it3 != ht.iend(); ++it3)
    {
        printf("%s:%s\n", it3->first.get().c_str(), it3->second.get().c_str());
        if (_stricmp(it3->first.get().c_str(), "5") == 0)
        {
            ht.eraseImpl(it3);
            auto it33 = ht.erase(it3);
        }

        if (_stricmp(it3->first.get().c_str(), "9") == 0)
        {
            ht.erase(it3);
        }
    }

    printf("pass2\n");
    for (auto it3 = ht.ibegin(); it3 != ht.iend(); ++it3)
    {
        printf("%s:%s\n", it3->first.get().c_str(), it3->second.get().c_str());
    }
*/
}