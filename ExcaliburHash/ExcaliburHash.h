#pragma once

#include <stdint.h>
#include <algorithm>
#include <type_traits>
#include <utility>

#if !defined(EXLBR_ALLOC) || !defined(EXLBR_FREE)
    #if defined(_WIN32)
        // Windows
        #include <xmmintrin.h>
        #define EXLBR_ALLOC(sizeInBytes, alignment) _mm_malloc(sizeInBytes, alignment)
        #define EXLBR_FREE(ptr) _mm_free(ptr)
    #else
        // Posix
        #include <stdlib.h>
        #define EXLBR_ALLOC(sizeInBytes, alignment) aligned_alloc(alignment, sizeInBytes)
        #define EXLBR_FREE(ptr) free(ptr)
    #endif
#endif

#if !defined(EXLBR_ASSERT)
    #include <assert.h>
    #define EXLBR_ASSERT(expression) assert(expression)
#endif

#if !defined(EXLBR_RESTRICT)
    #define EXLBR_RESTRICT __restrict
#endif

//
//
// KeyInfo for common types (TODO: move to separate header?)
//
//
namespace Excalibur
{

// generic type (without implementation)
template <typename T> struct KeyInfo
{
    //    static inline T getTombstone() noexcept;
    //    static inline T getEmpty() noexcept;
    //    static inline uint64_t hash(const T& key) noexcept;
    //    static inline bool isEqual(const T& lhs, const T& rhs) noexcept;
    //    static inline bool isValid(const T& key) noexcept;
};

template <> struct KeyInfo<int>
{
    static inline bool isValid(const int& key) noexcept { return key < 0x7ffffffe; }
    static inline int getTombstone() noexcept { return 0x7fffffff; }
    static inline int getEmpty() noexcept { return 0x7ffffffe; }
    static inline uint64_t hash(const int& key) noexcept { return key; }
    static inline bool isEqual(const int& lhs, const int& rhs) noexcept { return lhs == rhs; }
};

template <> struct KeyInfo<uint32_t>
{
    static inline bool isValid(const uint32_t& key) noexcept { return key < 0xfffffffe; }
    static inline uint32_t getTombstone() noexcept { return 0xfffffffe; }
    static inline uint32_t getEmpty() noexcept { return 0xffffffff; }
    static inline uint64_t hash(const uint32_t& key) noexcept { return key; }
    static inline bool isEqual(const uint32_t& lhs, const uint32_t& rhs) noexcept { return lhs == rhs; }
};

/*
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
*/

// TODO - add KeyInfo for all built-in types?

} // namespace Excalibur

namespace Excalibur
{

/*

TODO: Description

TODO: Design descisions/principles

TODO: Memory layout

*/
template <typename TKey, typename TValue, typename TKeyInfo = KeyInfo<TKey>> class HashTable
{
    static inline constexpr uint32_t k_MinNumberOfBuckets = 16;

    template <typename T> static inline T shr(T v, T shift) noexcept
    {
        static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value, "Type T should be an integral unsigned type.");
        return (v >> shift);
    }

    inline uint32_t nextPow2(uint32_t v)
    {
        EXLBR_ASSERT(v != 0);
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }

    struct has_values : std::bool_constant<!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value>
    {
    };

    template <typename T, class... Args> static void construct(void* EXLBR_RESTRICT ptr, Args&&... args)
    {
        new (ptr) T(std::forward<Args>(args)...);
    }
    template <typename T> void destruct(T* EXLBR_RESTRICT ptr) { ptr->~T(); }

    [[nodiscard]] static inline size_t align(size_t cursor, size_t alignment) noexcept
    {
        return (cursor + (alignment - 1)) & ~(alignment - 1);
    }
    [[nodiscard]] static inline bool isPointerAligned(void* cursor, size_t alignment) noexcept
    {
        return (uintptr_t(cursor) & (alignment - 1)) == 0;
    }

    template <typename TIterator> struct IteratorHelper
    {
        [[nodiscard]] static TIterator begin(const HashTable& ht) noexcept
        {
            if (ht.empty())
            {
                return end(ht);
            }

            TValue* EXLBR_RESTRICT value = ht.m_valuesStorage;
            TKey* EXLBR_RESTRICT key = ht.m_keysStorage;
            while (true)
            {
                if (TKeyInfo::isValid(*key))
                {
                    return TIterator(&ht, key, value);
                }
                key++;
                value++;
            }
        }

        [[nodiscard]] static TIterator end(const HashTable& ht) noexcept
        {
            const uint32_t numBuckets = ht.m_numBuckets;
            TKey* const endKey = ht.m_keysStorage + numBuckets;
            TValue* const endValue = ht.m_valuesStorage + numBuckets;
            return TIterator(&ht, endKey, endValue);
        }
    };

    template <typename THashTableSrc, typename THashTableDst> static inline void copyOrMoveTo(THashTableDst&& dst, THashTableSrc&& src)
    {
        EXLBR_ASSERT(dst.empty());
        if (src.empty())
        {
            return;
        }
        const uint32_t numBuckets = src.m_numBuckets;
        TValue* EXLBR_RESTRICT value = src.m_valuesStorage;
        TKey* EXLBR_RESTRICT key = src.m_keysStorage;
        TKey* const endKey = key + numBuckets;
        for (; key != endKey; key++, value++)
        {
            if (TKeyInfo::isValid(*key))
            {
                if constexpr (has_values::value)
                {
                    dst.emplace(std::move(*key), std::move(*value));
                }
                else
                {
                    dst.emplace(std::move(*key));
                }
            }
        }
        EXLBR_ASSERT(dst.size() == src.size());
    }

    inline void rehash()
    {
        uint32_t numBucketsNew = m_numBuckets * 2;
        HashTable newHash(numBucketsNew);
        copyOrMoveTo(newHash, std::move(*this));
        swap(newHash);
    }

    inline void allocateInline()
    {
        TKey* inlineKey = reinterpret_cast<TKey*>(&m_inlineKeysStorage);
        construct<TKey>(inlineKey, TKeyInfo::getEmpty());
        m_keysStorage = inlineKey;
    }

    inline void destroyInline()
    {
        if constexpr (!std::is_trivially_destructible<TKey>::value)
        {
            TKey* inlineKey = reinterpret_cast<TKey*>(&m_inlineKeysStorage);
            destruct(inlineKey);
        }
    }

    inline void create(uint32_t numBuckets)
    {
        allocateInline();

        numBuckets = (numBuckets < k_MinNumberOfBuckets) ? k_MinNumberOfBuckets : numBuckets;

        // numBuckets has to be power-of-two
        EXLBR_ASSERT(numBuckets > 0);
        EXLBR_ASSERT((numBuckets & (numBuckets - 1)) == 0);

        size_t numBytesKeys = numBuckets * sizeof(TKey);
        size_t numBytesKeysAligned = align(numBytesKeys, alignof(TValue));

        size_t alignment = alignof(TKey);
        size_t numBytesValues = 0;
        if constexpr (has_values::value)
        {
            numBytesValues = numBuckets * sizeof(TValue);
            alignment = std::max(alignment, alignof(TValue));
        }

        alignment = std::max(alignment, size_t(64)); // note: 64 to match CPU cache line size
        size_t numBytesTotal = numBytesKeysAligned + numBytesValues;
        numBytesTotal = align(numBytesTotal, alignment);

        EXLBR_ASSERT((numBytesTotal % alignment) == 0);

        void* raw = EXLBR_ALLOC(numBytesTotal, alignment);
        EXLBR_ASSERT(raw);
        m_keysStorage = reinterpret_cast<TKey*>(raw);
        EXLBR_ASSERT(raw == m_keysStorage);
        EXLBR_ASSERT(isPointerAligned(m_keysStorage, alignof(TKey)));

        if constexpr (has_values::value)
        {
            m_valuesStorage = reinterpret_cast<TValue*>((reinterpret_cast<char*>(raw) + numBytesKeysAligned));
            EXLBR_ASSERT(m_valuesStorage);
            EXLBR_ASSERT(isPointerAligned(m_valuesStorage, alignof(TValue)));
        }
        else
        {
            m_valuesStorage = nullptr;
        }

        m_numBuckets = numBuckets;
        m_numElements = 0;

        TKey* EXLBR_RESTRICT key = m_keysStorage;
        TKey* const endKey = key + numBuckets;
        for (; key != endKey; key++)
        {
            construct<TKey>(key, TKeyInfo::getEmpty());
        }
    }

    inline void destroy()
    {
        const uint32_t numBuckets = m_numBuckets;
        TValue* EXLBR_RESTRICT value = m_valuesStorage;
        TKey* EXLBR_RESTRICT key = m_keysStorage;
        TKey* const endKey = key + numBuckets;
        for (; key != endKey; key++, value++)
        {
            if (TKeyInfo::isValid(*key))
            {
                // destroy value
                if constexpr ((!std::is_trivially_destructible<TValue>::value) && (has_values::value))
                {
                    destruct(value);
                }
            }

            if constexpr (!std::is_trivially_destructible<TKey>::value)
            {
                destruct(key);
            }
        }
    }

    [[nodiscard]] inline TKey* findImpl(const TKey& key) const noexcept
    {
        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getTombstone(), key));
        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getEmpty(), key));
        const uint32_t numBuckets = m_numBuckets;
        TKey* const firstKey = m_keysStorage;
        TKey* const endKey = firstKey + numBuckets;
        const uint64_t hashValue = TKeyInfo::hash(key);
        uint32_t bucketIndex = uint32_t(hashValue) & (numBuckets - 1);
        TKey* EXLBR_RESTRICT currentKey = firstKey + bucketIndex;
        while (true)
        {
            if (TKeyInfo::isEqual(TKeyInfo::getEmpty(), *currentKey))
            {
                return endKey;
            }

            if (TKeyInfo::isEqual(key, *currentKey))
            {
                return currentKey;
            }
            currentKey++;
            currentKey = (currentKey == endKey) ? firstKey : currentKey;
        }
    }

    explicit HashTable(uint32_t _numChunks) { create(_numChunks); }

  public:
    class IteratorBase
    {
      protected:
        [[nodiscard]] inline const TValue* getValue() const noexcept
        {
            EXLBR_ASSERT(m_value >= m_ht->m_valuesStorage && m_value <= m_ht->m_valuesStorage + m_ht->m_numBuckets);
            return m_value;
        }
        [[nodiscard]] inline const TKey* getKey() const noexcept
        {
            EXLBR_ASSERT(m_key >= m_ht->m_keysStorage && m_key <= m_ht->m_keysStorage + m_ht->m_numBuckets);
            EXLBR_ASSERT(TKeyInfo::isValid(*m_key));
            return m_key;
        }

      public:
        IteratorBase() = delete;

        IteratorBase(const HashTable* ht, TKey* key, TValue* value) noexcept
            : m_ht(ht)
        {
            EXLBR_ASSERT(key >= ht->m_keysStorage && key <= ht->m_keysStorage + ht->m_numBuckets);
            EXLBR_ASSERT(value == nullptr || (ht->m_valuesStorage + size_t(key - ht->m_keysStorage)) == value);
            m_key = key;
            m_value = value;
        }

        IteratorBase(const HashTable* ht, TKey* key) noexcept
            : m_ht(ht)
        {
            TKey* const firstKey = ht->m_keysStorage;
            EXLBR_ASSERT(key >= firstKey && key <= (firstKey + ht->m_numBuckets));
            const size_t index = size_t(key - firstKey);
            EXLBR_ASSERT(firstKey + index == key);
            m_key = key;
            m_value = m_ht->m_valuesStorage + index;
        }

        bool operator==(const IteratorBase& other) const noexcept
        {
            // note: m_ht comparison is redundant and hence skipped
            return m_key == other.m_key && m_value == other.m_value;
        }
        bool operator!=(const IteratorBase& other) const noexcept
        {
            // note: m_ht comparison is redundant and hence skipped
            return m_key != other.m_key || m_value != other.m_value;
        }

        IteratorBase& operator++() noexcept
        {
            TKey* endKey = m_ht->m_keysStorage + m_ht->m_numBuckets;
            TKey* EXLBR_RESTRICT key = m_key;
            TValue* EXLBR_RESTRICT value = m_value;
            do
            {
                key++;
                value++;
            } while (key < endKey && !TKeyInfo::isValid(*key));

            m_key = key;
            m_value = value;
            return *this;
        }

        IteratorBase operator++(int) noexcept
        {
            IteratorBase res = *this;
            ++*this;
            return res;
        }

      protected:
        const HashTable* m_ht;
        TKey* m_key;
        TValue* m_value;
        friend class HashTable<TKey, TValue, TKeyInfo>;
    };

    class IteratorK : public IteratorBase
    {
      public:
        IteratorK() = delete;

        IteratorK(const HashTable* ht, TKey* key, TValue* value) noexcept
            : IteratorBase(ht, key, value)
        {
        }

        IteratorK(const HashTable* ht, TKey* key) noexcept
            : IteratorBase(ht, key)
        {
        }

        [[nodiscard]] inline const TKey& operator*() const noexcept { return *IteratorBase::getKey(); }
        [[nodiscard]] inline const TKey* operator->() const noexcept { return IteratorBase::getKey(); }
    };

    template <typename TIteratorValue> class TIteratorV : public IteratorBase
    {
      public:
        TIteratorV() = delete;

        TIteratorV(const HashTable* ht, TKey* key, TValue* value) noexcept
            : IteratorBase(ht, key, value)
        {
        }

        TIteratorV(const HashTable* ht, TKey* key) noexcept
            : IteratorBase(ht, key)
        {
        }

        [[nodiscard]] inline TIteratorValue& operator*() const noexcept { return *const_cast<TIteratorValue*>(IteratorBase::getValue()); }
        [[nodiscard]] inline TIteratorValue* operator->() const noexcept { return const_cast<TIteratorValue*>(IteratorBase::getValue()); }
    };

    template <typename TIteratorValue> class TIteratorKV : public IteratorBase
    {
      public:
        // pretty much similar to std::reference_wrapper, but supports late initialization
        template <typename TYPE> struct reference
        {
            TYPE* ptr = nullptr;

            explicit reference(TYPE* _ptr) noexcept
                : ptr(_ptr)
            {
            }

            reference(const reference&) noexcept = default;
            reference(reference&&) noexcept = default;
            reference& operator=(const reference&) noexcept = default;
            reference& operator=(reference&&) noexcept = default;
            void set(TYPE* _ptr) noexcept { ptr = _ptr; }
            TYPE& get() const noexcept
            {
                EXLBR_ASSERT(ptr);
                return *ptr;
            }

            operator TYPE&() const noexcept { return get(); }
        };

        using KeyValue = std::pair<const reference<const TKey>, const reference<TIteratorValue>>;

      private:
        void updateTmpKV() const noexcept
        {
            const reference<const TKey>& refKey = tmpKv.first;
            const_cast<reference<const TKey>&>(refKey).set(IteratorBase::getKey());
            const reference<TIteratorValue>& refVal = tmpKv.second;
            const_cast<reference<TIteratorValue>&>(refVal).set(const_cast<TIteratorValue*>(IteratorBase::getValue()));
        }

      public:
        TIteratorKV() = delete;

        TIteratorKV(const HashTable* ht, TKey* key, TValue* value) noexcept
            : IteratorBase(ht, key, value)
            , tmpKv(reference<const TKey>(nullptr), reference<TIteratorValue>(nullptr))
        {
        }

        TIteratorKV(const HashTable* ht, TKey* key) noexcept
            : IteratorBase(ht, key)
            , tmpKv(reference<const TKey>(nullptr), reference<TIteratorValue>(nullptr))
        {
        }

        [[nodiscard]] inline const TKey& key() const noexcept { return *IteratorBase::getKey(); }
        [[nodiscard]] inline TIteratorValue& value() const noexcept { return *const_cast<TIteratorValue*>(IteratorBase::getValue()); }
        [[nodiscard]] inline KeyValue& operator*() const noexcept
        {
            updateTmpKV();
            return tmpKv;
        }

        [[nodiscard]] inline KeyValue* operator->() const noexcept
        {
            updateTmpKV();
            return &tmpKv;
        }

      private:
        mutable KeyValue tmpKv;
    };

    using IteratorKV = TIteratorKV<TValue>;
    using ConstIteratorKV = TIteratorKV<const TValue>;
    using IteratorV = TIteratorV<TValue>;
    using ConstIteratorV = TIteratorV<const TValue>;

    HashTable() noexcept
        : m_valuesStorage(nullptr)
        , m_numBuckets(1)
        , m_numElements(0)
    {
        allocateInline();
    }

    ~HashTable()
    {
        const TKey* inlineKeyStorage = reinterpret_cast<const TKey*>(&m_inlineKeysStorage);
        if (inlineKeyStorage != m_keysStorage)
        {
            if constexpr (!std::is_trivially_destructible<TValue>::value || !std::is_trivially_destructible<TKey>::value)
            {
                destroy();
            }
            EXLBR_FREE(m_keysStorage);
        }
        destroyInline();
    }

    inline void swap(HashTable& other) noexcept
    {
        // note: due to using inline keyStorage this logic is a bit more complex than simple
        // std::swap(m_keysStorage, other.m_keysStorage)
        TKey* inlineKeyStorage = reinterpret_cast<TKey*>(&m_inlineKeysStorage);
        TKey* otherInlineKeyStorage = reinterpret_cast<TKey*>(&other.m_inlineKeysStorage);
        TKey* keysStorage = m_keysStorage;
        TKey* otherKeysStorage = other.m_keysStorage;
        m_keysStorage = (otherKeysStorage == otherInlineKeyStorage) ? inlineKeyStorage : otherKeysStorage;
        other.m_keysStorage = (keysStorage == inlineKeyStorage) ? otherInlineKeyStorage : keysStorage;

        std::swap(m_valuesStorage, other.m_valuesStorage);
        std::swap(m_numBuckets, other.m_numBuckets);
        std::swap(m_numElements, other.m_numElements);
    }

    inline void clear()
    {
        if (empty())
        {
            return;
        }

        const uint32_t numBuckets = m_numBuckets;
        TValue* EXLBR_RESTRICT value = m_valuesStorage;
        TKey* EXLBR_RESTRICT key = m_keysStorage;
        TKey* const endKey = key + numBuckets;
        for (; key != endKey; key++, value++)
        {
            if (TKeyInfo::isValid(*key))
            {
                // destroy value
                if constexpr ((!std::is_trivially_destructible<TValue>::value) && (has_values::value))
                {
                    destruct(value);
                }

                // overwrite key with empty key
                *key = TKeyInfo::getEmpty();
            }
        }
        // TODO: shrink if needed?
        m_numElements = 0;
    }

    template <typename TK, class... Args> inline std::pair<IteratorKV, bool> emplace(TK&& key, Args&&... args)
    {
        static_assert(std::is_same<TKey, typename std::remove_const<typename std::remove_reference<TK>::type>::type>::value,
                      "Expected unversal reference of TKey type");

        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getTombstone(), key));
        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getEmpty(), key));
        uint32_t numBuckets = m_numBuckets;

        // numBucketsThreshold = (numBuckets * 3/4) (but implemented using bit shifts)
        const uint32_t numBucketsThreshold = shr(numBuckets, 1u) + shr(numBuckets, 2u);
        if (m_numElements >= numBucketsThreshold)
        {
            rehash();
            numBuckets = m_numBuckets;
        }

        // numBuckets has to be power-of-two
        EXLBR_ASSERT(numBuckets > 0);
        EXLBR_ASSERT((numBuckets & (numBuckets - 1)) == 0);
        const uint64_t hashValue = TKeyInfo::hash(key);
        const uint32_t bucketIndex = uint32_t(hashValue) & (numBuckets - 1);
        TKey* const firstKey = m_keysStorage;
        TKey* const endKey = firstKey + numBuckets;
        TKey* EXLBR_RESTRICT currentKey = firstKey + bucketIndex;
        TKey* insertKey = nullptr;

        while (true)
        {
            if (TKeyInfo::isEqual(key, *currentKey))
            {
                return std::make_pair(IteratorKV(this, currentKey), false);
            }
            if (TKeyInfo::isEqual(TKeyInfo::getEmpty(), *currentKey))
            {
                insertKey = ((insertKey == nullptr) ? currentKey : insertKey);
                *insertKey = std::move(key);
                size_t index = size_t(insertKey - firstKey);
                TValue* value = m_valuesStorage + index;
                if constexpr (has_values::value)
                {
                    construct<TValue>(value, std::forward<Args>(args)...);
                }
                m_numElements++;
                return std::make_pair(IteratorKV(this, insertKey, value), true);
            }
            if (TKeyInfo::isEqual(TKeyInfo::getTombstone(), *currentKey) && insertKey == nullptr)
            {
                insertKey = currentKey;
            }
            currentKey++;
            currentKey = (currentKey == endKey) ? firstKey : currentKey;
        }
    }

    inline ConstIteratorKV find(const TKey& key) const noexcept
    {
        TKey* keyBucket = findImpl(key);
        return ConstIteratorKV(this, keyBucket);
    }
    inline IteratorKV find(const TKey& key) noexcept
    {
        TKey* keyBucket = findImpl(key);
        return IteratorKV(this, keyBucket);
    }

    inline bool erase(const IteratorBase it)
    {
        if (it == IteratorHelper<IteratorBase>::end(*this))
        {
            return false;
        }

        EXLBR_ASSERT(m_numElements != 0);
        m_numElements--;

        if constexpr ((!std::is_trivially_destructible<TValue>::value) && (has_values::value))
        {
            TValue* itemValue = const_cast<TValue*>(it.getValue());
            destruct(itemValue);
        }

        // overwrite key with empty key
        TKey* itemKey = const_cast<TKey*>(it.getKey());
        *itemKey = TKeyInfo::getTombstone();
        return true;
    }

    inline bool erase(const TKey& key)
    {
        auto it = find(key);
        return erase(it);
    }

    inline bool reserve(uint32_t numBuckets)
    {
        if (numBuckets == 0 || numBuckets < capacity())
        {
            return false;
        }
        numBuckets = nextPow2(numBuckets);
        HashTable newHash(numBuckets);
        copyOrMoveTo(newHash, std::move(*this));
        swap(newHash);
        return true;
    }

    [[nodiscard]] inline uint32_t size() const noexcept { return m_numElements; }
    [[nodiscard]] inline uint32_t capacity() const noexcept { return (m_numBuckets < k_MinNumberOfBuckets) ? 0 : m_numBuckets; }
    [[nodiscard]] inline bool empty() const noexcept { return (m_numElements == 0); }

    [[nodiscard]] inline bool has(const TKey& key) const noexcept { return (find(key) != iend()); }

    inline TValue& operator[](const TKey& key)
    {
        std::pair<IteratorKV, bool> it = emplace(key);
        return it.first.value();
    }

    [[nodiscard]] inline IteratorK begin() const { return IteratorHelper<IteratorK>::begin(*this); }
    [[nodiscard]] inline IteratorK end() const { return IteratorHelper<IteratorK>::end(*this); }

    [[nodiscard]] inline ConstIteratorV vbegin() const { return IteratorHelper<ConstIteratorV>::begin(*this); }
    [[nodiscard]] inline ConstIteratorV vend() const { return IteratorHelper<ConstIteratorV>::end(*this); }
    [[nodiscard]] inline IteratorV vbegin() { return IteratorHelper<IteratorV>::begin(*this); }
    [[nodiscard]] inline IteratorV vend() { return IteratorHelper<IteratorV>::end(*this); }

    [[nodiscard]] inline ConstIteratorKV ibegin() const { return IteratorHelper<ConstIteratorKV>::begin(*this); }
    [[nodiscard]] inline ConstIteratorKV iend() const { return IteratorHelper<ConstIteratorKV>::end(*this); }
    [[nodiscard]] inline IteratorKV ibegin() { return IteratorHelper<IteratorKV>::begin(*this); }
    [[nodiscard]] inline IteratorKV iend() { return IteratorHelper<IteratorKV>::end(*this); }

    template <typename TIterator> struct TypedIteratorHelper
    {
        const HashTable* ht;
        TypedIteratorHelper(const HashTable* _ht)
            : ht(_ht)
        {
        }
        TIterator begin() { return IteratorHelper<TIterator>::begin(*ht); }
        TIterator end() { return IteratorHelper<TIterator>::end(*ht); }
    };

    using Keys = TypedIteratorHelper<IteratorK>;
    using Values = TypedIteratorHelper<IteratorV>;
    using Items = TypedIteratorHelper<IteratorKV>;
    using ConstValues = TypedIteratorHelper<ConstIteratorV>;
    using ConstItems = TypedIteratorHelper<ConstIteratorKV>;

    [[nodiscard]] inline Keys keys() const { return Keys(this); }
    [[nodiscard]] inline ConstValues values() const { return ConstValues(this); }
    [[nodiscard]] inline ConstItems items() const { return ConstItems(this); }

    [[nodiscard]] inline Values values() { return Values(this); }
    [[nodiscard]] inline Items items() { return Items(this); }

    // copy ctor
    HashTable(const HashTable& other)
    {
        create(other.m_numBuckets);
        copyOrMoveTo(*this, other);
    }

    // copy assignment
    HashTable& operator=(const HashTable& other)
    {
        uint32_t numBucketsCopy = m_numBuckets;
        HashTable hashCopy(numBucketsCopy);
        copyOrMoveTo(hashCopy, other);
        swap(hashCopy);
        return *this;
    }

    // move ctor
    HashTable(HashTable&& other)
        : m_valuesStorage(nullptr)
        , m_numBuckets(1)
        , m_numElements(0)
    {
        allocateInline();
        swap(other);
    }

    // move assignment
    HashTable& operator=(HashTable&& other)
    {
        swap(other);
        return *this;
    }

  private:
    // prefix m_ to be able to easily see member access from the code (it could be more expensive in the inner loop)
    TKey* m_keysStorage;     // 8
    TValue* m_valuesStorage; // 8
    uint32_t m_numBuckets;   // 4
    uint32_t m_numElements;  // 4

    // we need this key to keep m_keysStorage not null all the time. this will save us a few null pointer checks
    typename std::aligned_storage<sizeof(TKey), alignof(TKey)>::type m_inlineKeysStorage;
    static_assert(sizeof(m_inlineKeysStorage) == sizeof(TKey), "Incorrect sizeof");
};

} // namespace Excalibur
