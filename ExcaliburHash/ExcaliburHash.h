#pragma once

#include <algorithm>
#include <stdint.h>
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
/*
    #define EXLBR_ASSERT(expr)                                                                                                             \
        do                                                                                                                                 \
        {                                                                                                                                  \
            if (!(expr))                                                                                                                   \
                _CrtDbgBreak();                                                                                                            \
        } while (0)
*/
#endif

#if !defined(EXLBR_RESTRICT)
    #define EXLBR_RESTRICT __restrict
#endif

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

/*
template <> struct KeyInfo<uint32_t>
{
    static inline bool isValid(const uint32_t& key) noexcept { return key < 0xfffffffe; }
    static inline uint32_t getTombstone() noexcept { return 0xfffffffe; }
    static inline uint32_t getEmpty() noexcept { return 0xffffffff; }
    static inline uint64_t hash(const uint32_t& key) noexcept { return key; }
    static inline bool isEqual(const uint32_t& lhs, const uint32_t& rhs) noexcept { return lhs == rhs; }
};

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
    struct has_values : std::bool_constant<!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value>
    {
    };

    static inline constexpr uint32_t k_MinNumberOfBuckets = 16;

    template <typename T, class... Args> static void construct(void* EXLBR_RESTRICT ptr, Args&&... args)
    {
        new (ptr) T(std::forward<Args>(args)...);
    }
    template <typename T> static void destruct(T* EXLBR_RESTRICT ptr) { ptr->~T(); }

    template <bool hasValue, typename dummy = void> struct Storage
    {
    };

    template <typename dummy> struct Storage<true, dummy>
    {
        struct TItem
        {
            using TValueStorage = typename std::aligned_storage<sizeof(TValue), alignof(TValue)>::type;
            TKey m_key;
            TValueStorage m_value;

            inline TItem(TKey&& other) noexcept
                : m_key(std::move(other))
            {
            }
            [[nodiscard]] inline bool isValid() const noexcept { return TKeyInfo::isValid(m_key); }
            [[nodiscard]] inline bool isEmpty() const noexcept { return TKeyInfo::isEqual(TKeyInfo::getEmpty(), m_key); }
            [[nodiscard]] inline bool isTombstone() const noexcept { return TKeyInfo::isEqual(TKeyInfo::getTombstone(), m_key); }
            [[nodiscard]] inline bool isEqual(const TKey& key) const noexcept { return TKeyInfo::isEqual(key, m_key); }

            [[nodiscard]] inline TKey* key() noexcept { return &m_key; }
            [[nodiscard]] inline TValue* value() noexcept
            {
                TValue* value = reinterpret_cast<TValue*>(&m_value);
                return value;
            }
        };
    };

    template <typename dummy> struct Storage<false, dummy>
    {
        struct TItem
        {
            TKey m_key;

            inline TItem(TKey&& other) noexcept
                : m_key(std::move(other))
            {
            }
            [[nodiscard]] inline bool isValid() const noexcept { return TKeyInfo::isValid(m_key); }
            [[nodiscard]] inline bool isEmpty() const noexcept { return TKeyInfo::isEqual(TKeyInfo::getEmpty(), m_key); }
            [[nodiscard]] inline bool isTombstone() const noexcept { return TKeyInfo::isEqual(TKeyInfo::getTombstone(), m_key); }
            [[nodiscard]] inline bool isEqual(const TKey& key) const noexcept { return TKeyInfo::isEqual(key, m_key); }

            [[nodiscard]] inline TKey* key() noexcept { return &m_key; }
            // inline TValue* value() noexcept{ return nullptr; }
        };
    };

    using TItem = typename Storage<has_values::value>::TItem;

    template <typename T> static inline T shr(T v, T shift) noexcept
    {
        static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value, "Type T should be an integral unsigned type.");
        return (v >> shift);
    }

    [[nodiscard]] inline uint32_t nextPow2(uint32_t v) noexcept
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

            TItem* EXLBR_RESTRICT item = ht.m_storage;
            while (true)
            {
                if (item->isValid())
                {
                    return TIterator(&ht, item);
                }
                item++;
            }
        }

        [[nodiscard]] static TIterator end(const HashTable& ht) noexcept
        {
            const uint32_t numBuckets = ht.m_numBuckets;
            TItem* const endItem = ht.m_storage + numBuckets;
            return TIterator(&ht, endItem);
        }
    };

    inline void moveFrom(HashTable&& other)
    {
        // note: the current hash table is supposed to be destroyed/non-initialized

        if (!other.isUsingInlineStorage())
        {
            // if not using inline storage than it's a simple pointer swap
            allocateInline(TKeyInfo::getEmpty());
            m_storage = other.m_storage;
            m_numBuckets = other.m_numBuckets;
            m_numElements = other.m_numElements;
            other.m_storage = nullptr;
            // other.m_numBuckets = 0;
            // other.m_numElements = 0;
        }
        else
        {
            // if using inline storage than let's move items from one inline storage into another
            TItem* otherInlineItem = reinterpret_cast<TItem*>(&other.m_inlineStorage);
            bool hasValidValue = otherInlineItem->isValid();
            TItem* inlineItem = allocateInline(std::move(*otherInlineItem->key()));

            if constexpr (has_values::value)
            {
                // move inline storage value (if any)
                if (hasValidValue)
                {
                    TValue* value = inlineItem->value();
                    TValue* otherValue = otherInlineItem->value();
                    construct<TValue>(value, std::move(*otherValue));
                    destruct(otherValue);
                }
            }

            m_storage = inlineItem;
            m_numBuckets = other.m_numBuckets;
            m_numElements = other.m_numElements;
            // destruct(otherInlineItem);
            other.m_storage = nullptr;
            // other.m_numBuckets = 0;
            // other.m_numElements = 0;
        }
    }

    inline void copyFrom(const HashTable& other)
    {
        if (other.empty())
        {
            return;
        }

        const uint32_t numBuckets = other.m_numBuckets;
        TItem* EXLBR_RESTRICT item = other.m_storage;
        TItem* const enditem = item + numBuckets;
        for (; item != enditem; item++)
        {
            if (item->isValid())
            {
                if constexpr (has_values::value)
                {
                    emplace(*item->key(), *item->value());
                }
                else
                {
                    emplace(*item->key());
                }
            }
        }
    }

    [[nodiscard]] inline bool isUsingInlineStorage() const noexcept
    {
        const TItem* inlineStorage = reinterpret_cast<const TItem*>(&m_inlineStorage);
        return (inlineStorage == m_storage);
    }

    template <class... Args> inline TItem* allocateInline(Args&&... args)
    {
        TItem* inlineItem = reinterpret_cast<TItem*>(&m_inlineStorage);
        construct<TItem>(inlineItem, std::forward<Args>(args)...);
        return inlineItem;
    }

    inline uint32_t create(uint32_t numBuckets)
    {
        numBuckets = (numBuckets < k_MinNumberOfBuckets) ? k_MinNumberOfBuckets : numBuckets;

        // numBuckets has to be power-of-two
        EXLBR_ASSERT(numBuckets > 0);
        EXLBR_ASSERT((numBuckets & (numBuckets - 1)) == 0);

        size_t numBytes = sizeof(TItem) * numBuckets;
        // note: 64 to match CPU cache line size
        size_t alignment = std::max(alignof(TItem), size_t(64));
        numBytes = align(numBytes, alignment);
        EXLBR_ASSERT((numBytes % alignment) == 0);

        void* raw = EXLBR_ALLOC(numBytes, alignment);
        EXLBR_ASSERT(raw);
        m_storage = reinterpret_cast<TItem*>(raw);
        EXLBR_ASSERT(raw == m_storage);
        EXLBR_ASSERT(isPointerAligned(m_storage, alignof(TItem)));

        m_numBuckets = numBuckets;
        m_numElements = 0;

        TItem* EXLBR_RESTRICT item = m_storage;
        TItem* const endItem = item + numBuckets;
        for (; item != endItem; item++)
        {
            construct<TItem>(item, TKeyInfo::getEmpty());
        }

        return numBuckets;
    }

    inline void destroy()
    {
        const uint32_t numBuckets = m_numBuckets;
        TItem* EXLBR_RESTRICT item = m_storage;
        TItem* const endItem = item + numBuckets;
        for (; item != endItem; item++)
        {
            destruct(item);
        }
    }

    inline void destroyAndFreeMemory()
    {
        if constexpr (!std::is_trivially_destructible<TValue>::value || !std::is_trivially_destructible<TKey>::value)
        {
            destroy();
        }

        if (!isUsingInlineStorage())
        {
            EXLBR_FREE(m_storage);
        }
    }

    [[nodiscard]] inline TItem* findImpl(const TKey& key) const noexcept
    {
        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getTombstone(), key));
        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getEmpty(), key));
        const uint32_t numBuckets = m_numBuckets;
        TItem* const firstItem = m_storage;
        TItem* const endItem = firstItem + numBuckets;
        const uint64_t hashValue = TKeyInfo::hash(key);
        uint32_t bucketIndex = uint32_t(hashValue) & (numBuckets - 1);
        TItem* startItem = firstItem + bucketIndex;
        TItem* EXLBR_RESTRICT currentItem = startItem;
        do
        {
            if (currentItem->isEmpty())
            {
                return endItem;
            }

            if (currentItem->isEqual(key))
            {
                return currentItem;
            }
            currentItem++;
            currentItem = (currentItem == endItem) ? firstItem : currentItem;
        } while (currentItem != startItem);
        return endItem;
    }

  public:
    class IteratorBase
    {
      protected:
        [[nodiscard]] inline const TKey* getKey() const noexcept
        {
            EXLBR_ASSERT(m_item->isValid());
            return m_item->key();
        }
        [[nodiscard]] inline const TValue* getValue() const noexcept
        {
            EXLBR_ASSERT(m_item->isValid());
            return m_item->value();
        }

      public:
        IteratorBase() = delete;

        IteratorBase(const HashTable* ht, TItem* item) noexcept
            : m_ht(ht)
            , m_item(item)
        {
        }

        bool operator==(const IteratorBase& other) const noexcept
        {
            // note: m_ht comparison is redundant and hence skipped
            return m_item == other.m_item;
        }
        bool operator!=(const IteratorBase& other) const noexcept
        {
            // note: m_ht comparison is redundant and hence skipped
            return m_item != other.m_item;
        }

        IteratorBase& operator++() noexcept
        {
            TItem* endItem = m_ht->m_storage + m_ht->m_numBuckets;
            TItem* EXLBR_RESTRICT item = m_item;
            do
            {
                item++;
            } while (item < endItem && !item->isValid());

            m_item = item;
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
        TItem* m_item;
        friend class HashTable<TKey, TValue, TKeyInfo>;
    };

    class IteratorK : public IteratorBase
    {
      public:
        IteratorK() = delete;

        IteratorK(const HashTable* ht, TItem* item) noexcept
            : IteratorBase(ht, item)
        {
        }

        [[nodiscard]] inline const TKey& operator*() const noexcept { return *IteratorBase::getKey(); }
        [[nodiscard]] inline const TKey* operator->() const noexcept { return IteratorBase::getKey(); }
    };

    template <typename TIteratorValue> class TIteratorV : public IteratorBase
    {
      public:
        TIteratorV() = delete;

        TIteratorV(const HashTable* ht, TItem* item) noexcept
            : IteratorBase(ht, item)
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

        TIteratorKV(const HashTable* ht, TItem* item) noexcept
            : IteratorBase(ht, item)
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
        //: m_storage(nullptr)
        : m_numBuckets(1)
        , m_numElements(0)
    {
        m_storage = allocateInline(TKeyInfo::getEmpty());
    }

    ~HashTable()
    {
        if (m_storage)
        {
            destroyAndFreeMemory();
        }
    }

    inline void clear()
    {
        if (empty())
        {
            return;
        }

        const uint32_t numBuckets = m_numBuckets;
        TItem* EXLBR_RESTRICT item = m_storage;
        TItem* const endItem = item + numBuckets;
        for (; item != endItem; item++)
        {
            // destroy value if need
            if constexpr (!std::is_trivially_destructible<TValue>::value)
            {
                if (item->isValid())
                {
                    destruct(item->value());
                }
            }

            // set key to empty
            *item->key() = TKeyInfo::getEmpty();
        }
        // TODO: shrink if needed?
        m_numElements = 0;
    }

  private:
    template <typename TK, class... Args>
    inline std::pair<IteratorKV, bool> emplaceToExisting(uint32_t numBuckets, TK&& key, Args&&... args)
    {
        // numBuckets has to be power-of-two
        EXLBR_ASSERT(numBuckets > 0);
        EXLBR_ASSERT((numBuckets & (numBuckets - 1)) == 0);
        const uint64_t hashValue = TKeyInfo::hash(key);
        const uint32_t bucketIndex = uint32_t(hashValue) & (numBuckets - 1);
        TItem* const firstItem = m_storage;
        TItem* const endItem = firstItem + numBuckets;
        TItem* EXLBR_RESTRICT currentItem = firstItem + bucketIndex;
        TItem* insertItem = nullptr;

        while (true)
        {
            if (currentItem->isEqual(key))
            {
                return std::make_pair(IteratorKV(this, currentItem), false);
            }
            if (currentItem->isEmpty())
            {
                insertItem = ((insertItem == nullptr) ? currentItem : insertItem);

                // move key
                *insertItem->key() = std::move(key);
                // construct value if need
                if constexpr (has_values::value)
                {
                    construct<TValue>(insertItem->value(), std::forward<Args>(args)...);
                }
                m_numElements++;
                return std::make_pair(IteratorKV(this, insertItem), true);
            }
            if (currentItem->isTombstone() && insertItem == nullptr)
            {
                insertItem = currentItem;
            }
            currentItem++;
            currentItem = (currentItem == endItem) ? firstItem : currentItem;
        }
    }

    inline void reinsert(uint32_t numBucketsNew, TItem* EXLBR_RESTRICT item, TItem* const enditem) noexcept
    {
        // re-insert existing elements
        for (; item != enditem; item++)
        {
            if (item->isValid())
            {
                if constexpr (has_values::value)
                {
                    emplaceToExisting(numBucketsNew, std::move(*item->key()), std::move(*item->value()));
                }
                else
                {
                    emplaceToExisting(numBucketsNew, std::move(*item->key()));
                }
            }
            destruct(item);
        }
    }

    template <typename TK, class... Args>
    inline std::pair<IteratorKV, bool> emplaceReallocate(uint32_t numBucketsNew, TK&& key, Args&&... args)
    {
        const uint32_t numBuckets = m_numBuckets;
        TItem* storage = m_storage;
        TItem* EXLBR_RESTRICT item = storage;
        TItem* const enditem = item + numBuckets;
        bool isInlineStorage = isUsingInlineStorage();

        numBucketsNew = create(numBucketsNew);

        //
        // insert a new element (one of the args might still point to the old storage in case of hash table 'aliasing'
        //
        // i.e.
        // auto it = table.find("key");
        // table.emplace("another_key", it->second);   // <--- when hash table grows it->second will point to a memory we are about to free
        auto it = emplaceToExisting(numBucketsNew, key, args...);

        reinsert(numBucketsNew, item, enditem);

        if (!isInlineStorage)
        {
            EXLBR_FREE(storage);
        }

        return it;
    }

  public:
    template <typename TK, class... Args> inline std::pair<IteratorKV, bool> emplace(TK&& key, Args&&... args)
    {
        static_assert(std::is_same<TKey, typename std::remove_const<typename std::remove_reference<TK>::type>::type>::value,
                      "Expected unversal reference of TKey type. Wrong key type?");

        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getTombstone(), key));
        EXLBR_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getEmpty(), key));
        uint32_t numBuckets = m_numBuckets;

        // numBucketsThreshold = (numBuckets * 3/4) (but implemented using bit shifts)
        const uint32_t numBucketsThreshold = shr(numBuckets, 1u) + shr(numBuckets, 2u);
        if (m_numElements > numBucketsThreshold)
        {
            return emplaceReallocate(numBuckets * 2, key, args...);
        }

        return emplaceToExisting(numBuckets, key, args...);
    }

    [[nodiscard]] inline ConstIteratorKV find(const TKey& key) const noexcept
    {
        TItem* item = findImpl(key);
        return ConstIteratorKV(this, item);
    }
    [[nodiscard]] inline IteratorKV find(const TKey& key) noexcept
    {
        TItem* item = findImpl(key);
        return IteratorKV(this, item);
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

        // hash table now is empty. convert all tombstones to empty keys
        if (m_numElements == 0)
        {
            TItem* EXLBR_RESTRICT item = m_storage;
            TItem* const endItem = item + m_numBuckets;
            for (; item != endItem; item++)
            {
                *item->key() = TKeyInfo::getEmpty();
            }
            return true;
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

    inline bool reserve(uint32_t numBucketsNew)
    {
        if (numBucketsNew == 0 || numBucketsNew < capacity())
        {
            return false;
        }
        numBucketsNew = nextPow2(numBucketsNew);

        const uint32_t numBuckets = m_numBuckets;
        TItem* storage = m_storage;
        TItem* EXLBR_RESTRICT item = storage;
        TItem* const enditem = item + numBuckets;
        bool isInlineStorage = isUsingInlineStorage();

        numBucketsNew = create(numBucketsNew);

        reinsert(numBucketsNew, item, enditem);

        if (!isInlineStorage)
        {
            EXLBR_FREE(storage);
        }

        return true;
    }

    [[nodiscard]] inline uint32_t size() const noexcept { return m_numElements; }
    [[nodiscard]] inline uint32_t capacity() const noexcept { return m_numBuckets; }
    [[nodiscard]] inline bool empty() const noexcept { return (m_numElements == 0); }

    [[nodiscard]] inline bool has(const TKey& key) const noexcept { return (find(key) != iend()); }

    inline TValue& operator[](const TKey& key)
    {
        IteratorKV it = find(key);
        if (it != iend())
        {
            return it.value();
        }
        // note: we can not use `emplace()` without calling `find()` function first
        // because calling `emplace()` function might grow the hash table even if a key exists in the table (which will invalidate existing
        // iterators)
        std::pair<IteratorKV, bool> emplaceIt = emplace(key);
        return emplaceIt.first.value();
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
        EXLBR_ASSERT(&other != this);
        m_storage = allocateInline(TKeyInfo::getEmpty());
        create(other.m_numBuckets);
        copyFrom(other);
    }

    // copy assignment
    HashTable& operator=(const HashTable& other)
    {
        if (&other == this)
        {
            return *this;
        }
        destroyAndFreeMemory();
        m_storage = allocateInline(TKeyInfo::getEmpty());
        create(other.m_numBuckets);
        copyFrom(other);
        return *this;
    }

    // move ctor
    HashTable(HashTable&& other) noexcept
    //: m_storage(nullptr)
    //, m_numBuckets(1)
    //, m_numElements(0)
    {
        EXLBR_ASSERT(&other != this);
        moveFrom(std::move(other));
    }

    // move assignment
    HashTable& operator=(HashTable&& other) noexcept
    {
        if (&other == this)
        {
            return *this;
        }
        destroyAndFreeMemory();
        moveFrom(std::move(other));
        return *this;
    }

  private:
    // prefix m_ to be able to easily see member access from the code (it could be more expensive in the inner loop)
    TItem* m_storage;       // 8
    uint32_t m_numBuckets;  // 4
    uint32_t m_numElements; // 4

    // We need this inline storage to keep `m_storage` not null all the time.
    // This will save us from `empty()` check inside `find()` function implementation
    typename std::aligned_storage<sizeof(TItem), alignof(TItem)>::type m_inlineStorage;
    static_assert(sizeof(m_inlineStorage) == sizeof(TItem), "Incorrect sizeof");
};

} // namespace Excalibur
