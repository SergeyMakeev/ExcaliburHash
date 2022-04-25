#pragma once

#include <stdint.h>
//#include <string>
#include <algorithm> // min/max
#include <type_traits>
#include <utility>

#if !defined(CHHT_ALLOC) || !defined(CHHT_FREE)
    #if defined(_WIN32)
        // Windows
        #include <xmmintrin.h>
        #define CHHT_ALLOC(sizeInBytes, alignment) _mm_malloc(sizeInBytes, alignment)
        #define CHHT_FREE(ptr) _mm_free(ptr)
    #else
        // Posix
        #include <stdlib.h>
        #define CHHT_ALLOC(sizeInBytes, alignment) aligned_alloc(alignment, sizeInBytes)
        #define CHHT_FREE(ptr) free(ptr)
    #endif
#endif

#if !defined(CHHT_ASSERT)
    #include <assert.h>
    #define CHHT_ASSERT(expression) assert(expression)
/*
    #define CHHT_ASSERT(expr)                                                                                                              \
        do                                                                                                                                 \
        {                                                                                                                                  \
            if (!(expr))                                                                                                                   \
                _CrtDbgBreak();                                                                                                            \
        } while (0)
*/
#endif

#if !defined(CHHT_RESTRICT)
    #define CHHT_RESTRICT __restrict
#endif

//#define CHHT_USED_IN_ASSERT(x) (void)(x)

/*

 http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn

const int tab32[32] = {
     0,  9,  1, 10, 13, 21,  2, 29,
    11, 14, 16, 18, 22, 25,  3, 30,
     8, 12, 20, 28, 15, 17, 24,  7,
    19, 27, 23,  6, 26,  5,  4, 31};

int log2_32 (uint32_t value)
{
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return tab32[(uint32_t)(value*0x07C4ACDD) >> 27];
}

*/

//
//
// KeyInfo for common types
//
//
namespace Excalibur
{

// generic type (without implementation)
template <typename T> struct KeyInfo
{
    static inline T getEmpty() noexcept;                /*{ static_assert(false, "Please provide a template specialization for type T"); }*/
    static inline uint64_t hash(const T& key) noexcept; /*{ static_assert(false, "Please provide a template specialization for type T"); }*/
    static inline bool isEqual(const T& lhs, const T& rhs) noexcept; /*
    {
        static_assert(false, "Please provide a template specialization for type T");
    }
    */
};

template <> struct KeyInfo<int>
{
    static inline int getEmpty() noexcept { return 0x7fffffff; }
    static inline uint64_t hash(const int& key) noexcept { return key; }
    static inline bool isEqual(const int& lhs, const int& rhs) noexcept { return lhs == rhs; }
};

template <> struct KeyInfo<uint32_t>
{
    static inline uint32_t getEmpty() noexcept { return 0xffffffff; }
    static inline uint64_t hash(const uint32_t& key) noexcept { return key; }
    static inline bool isEqual(const uint32_t& lhs, const uint32_t& rhs) noexcept { return lhs == rhs; }
};

/*
template <> struct KeyInfo<std::string>
{
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
    using KeyStorage = typename std::aligned_storage<sizeof(TKey), alignof(TKey)>::type;
    using ValueStorage = typename std::aligned_storage<sizeof(TValue), alignof(TValue)>::type;

    static inline constexpr uint32_t kMinNumberOfChunks = 2;
    static inline constexpr uint64_t kNumElementsInChunk = 16;

    struct ItemAddr
    {
        // chunk index (upper 56 bits)
        size_t chunk;

        // index within chunk (lower 6 bits) (TODO: use smaller data type?)
        size_t subIndex;
    };

    template <typename T> static inline T shr(T v, T shift) noexcept
    {
        static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value, "Type T should be an integral unsigned type.");
        return (v >> shift);
    }

    /*

    convert hash value to entry point address (chunk + slot)

    */
    static inline ItemAddr hashToEntryPointAddress(uint64_t hashValue, size_t chunkMod) noexcept
    {
        CHHT_ASSERT(chunkMod > 0);
        const size_t subIndex = size_t(hashValue % kNumElementsInChunk);
        const size_t chunk = (size_t(hashValue / kNumElementsInChunk) & chunkMod);
        return {chunk, subIndex};
    }

    struct Chunk
    {
        union
        {
            struct
            {
                uint16_t numEntries;
                uint16_t maxProbeLength;
            } meta;
            struct
            {
                uint32_t meta;
            } fc;
        } header;
        KeyStorage keys[kNumElementsInChunk];
    };

    template <typename T, class... Args> static void construct(void* CHHT_RESTRICT ptr, Args&&... args)
    {
        new (ptr) T(std::forward<Args>(args)...);
    }
    template <typename T> void destruct(T* CHHT_RESTRICT ptr) { ptr->~T(); }

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
        static TIterator begin(const HashTable& ht) noexcept
        {
            if (ht.empty())
            {
                return end(ht);
            }

            Chunk* const CHHT_RESTRICT lastValidChunk = ht.lastChunk;
            Chunk* CHHT_RESTRICT chunk = &ht.chunksStorage[0];
            size_t firstValidIndex = 0;
            for (; chunk <= lastValidChunk; chunk++)
            {
                TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[0]);
                TKey* CHHT_RESTRICT endItemKey = reinterpret_cast<TKey*>(&chunk->keys[kNumElementsInChunk]);
                for (; itemKey < endItemKey; itemKey++, firstValidIndex++)
                {
                    if (!TKeyInfo::isEqual(TKeyInfo::getEmpty(), *itemKey))
                    {
                        size_t chunkIndex = firstValidIndex / size_t(kNumElementsInChunk);
                        size_t chunkSubIndex = firstValidIndex % size_t(kNumElementsInChunk);

                        const uint64_t hashValue = TKeyInfo::hash(*itemKey);
                        const ItemAddr addr = hashToEntryPointAddress(hashValue, (ht.numChunks - 1));
                        return TIterator(&ht, chunkIndex, chunkSubIndex, addr.chunk);
                    }
                }
            }
            // hash table is not empty, but there is no entry point found? this should never happen
            CHHT_ASSERT(false);
            return end(ht);
        }

        static TIterator end(const HashTable& ht) noexcept { return TIterator(&ht, ht.numChunks, 0, 0); }
    };

    template <typename THashTableSrc, typename THashTableDst> static inline void copyOrMoveTo(THashTableDst&& dst, THashTableSrc&& src)
    {
        if (src.empty())
        {
            return;
        }

        const uint32_t numBuckets = src.numChunks * kNumElementsInChunk;

        Chunk* CHHT_RESTRICT chunk = &src.chunksStorage[0];
        for (size_t globalIndex = 0; globalIndex < numBuckets;)
        {
            TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[0]);
            for (size_t subIndex = 0; subIndex < kNumElementsInChunk; subIndex++, globalIndex++, itemKey++)
            {
                if (!TKeyInfo::isEqual(TKeyInfo::getEmpty(), *itemKey))
                {
                    if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
                    {
                        TValue* itemValue = reinterpret_cast<TValue*>(&src.valuesStorage[globalIndex]);
                        dst.emplace(std::move(*itemKey), std::move(*itemValue));
                    }
                    else
                    {
                        dst.emplace(std::move(*itemKey));
                    }
                }
            }
            chunk++;
        }
    }

    inline void rehash()
    {
        uint32_t newNumChunks = numChunks * 2;
        HashTable newHash(newNumChunks);
        copyOrMoveTo(newHash, std::move(*this));
        swap(newHash);
    }

    inline void rehash_if_need()
    {
        const uint32_t _numBuckets = capacity();
        // numBucketsThreshold = (numBuckets * 3/4) (but implemented using bit shifts)
        const uint32_t _numBucketsDiv2 = shr(_numBuckets, 1u);
        const uint32_t _numBucketsDiv4 = shr(_numBuckets, 2u);
        const uint32_t numBucketsThreshold = _numBucketsDiv2 + _numBucketsDiv4;
        const bool reshashBecauseLoadFactorIsTooHigh = (numElements >= numBucketsThreshold);
        if (reshashBecauseLoadFactorIsTooHigh)
        {
            rehash();
        }
    }

    inline void create(uint32_t _numChunks)
    {
        _numChunks = (_numChunks < kMinNumberOfChunks) ? kMinNumberOfChunks : _numChunks;

        // numBuckets has to be power-of-two
        CHHT_ASSERT(_numChunks > 0);
        CHHT_ASSERT((_numChunks & (_numChunks - 1)) == 0);

        size_t numBytesKeys = _numChunks * sizeof(Chunk);
        size_t numBytesKeysAligned = align(numBytesKeys, alignof(ValueStorage));

        size_t alignment = alignof(Chunk);
        size_t numBytesValues = 0;
        if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
        {
            numBytesValues = _numChunks * sizeof(ValueStorage) * kNumElementsInChunk;
            alignment = std::max(alignment, alignof(ValueStorage));
        }

        alignment = std::max(alignment, size_t(16));
        size_t numBytesTotal = numBytesKeysAligned + numBytesValues;
        numBytesTotal = align(numBytesTotal, alignment);

        CHHT_ASSERT((numBytesTotal % alignment) == 0);

        void* raw = CHHT_ALLOC(numBytesTotal, alignment);
        CHHT_ASSERT(raw);
        chunksStorage = (Chunk*)raw;
        CHHT_ASSERT(raw == chunksStorage);
        CHHT_ASSERT(chunksStorage);
        CHHT_ASSERT(isPointerAligned(chunksStorage, alignof(Chunk)));

        if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
        {
            valuesStorage = (ValueStorage*)(reinterpret_cast<char*>(raw) + numBytesKeysAligned);
            CHHT_ASSERT(valuesStorage);
            CHHT_ASSERT(isPointerAligned(valuesStorage, alignof(ValueStorage)));
        }
        else
        {
            valuesStorage = nullptr;
        }

        CHHT_ASSERT(_numChunks > 0);
        numChunks = _numChunks;
        numElements = 0;

        lastChunk = &chunksStorage[_numChunks - 1];

        // initialize keys
        Chunk* CHHT_RESTRICT chunk = &chunksStorage[0];
        Chunk* const CHHT_RESTRICT lastValidChunk = lastChunk;

        for (; chunk <= lastValidChunk; chunk++)
        {
            chunk->header.fc.meta = 0;
            TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[0]);
            for (size_t i = 0; i < kNumElementsInChunk; i++, itemKey++)
            {
                // fill with empty keys
                if constexpr (std::is_trivially_constructible<TKey>::value)
                {
                    *itemKey = TKeyInfo::getEmpty();
                }
                else
                {
                    construct<TKey>(itemKey, TKeyInfo::getEmpty());
                }
            }
        }
    }

    inline void destroy()
    {
        if (empty())
        {
            return;
        }

        Chunk* const CHHT_RESTRICT lastValidChunk = lastChunk;
        Chunk* CHHT_RESTRICT chunk = &chunksStorage[0];

        TValue* CHHT_RESTRICT itemValue = nullptr;
        if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
        {
            itemValue = reinterpret_cast<TValue*>(&valuesStorage[0]);
        }

        for (; chunk <= lastValidChunk; chunk++)
        {
            TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[0]);
            TKey* CHHT_RESTRICT endItemKey = reinterpret_cast<TKey*>(&chunk->keys[kNumElementsInChunk]);
            for (; itemKey < endItemKey; itemValue++, itemKey++)
            {
                if constexpr ((!std::is_trivially_destructible<TValue>::value) &&
                              (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value))
                {
                    if (!TKeyInfo::isEqual(TKeyInfo::getEmpty(), *itemKey))
                    {
                        destruct(itemValue);
                    }
                }

                if constexpr (!std::is_trivially_destructible<TKey>::value)
                {
                    destruct(itemKey);
                }
            }
        }
    }

    template <typename TIteratorType> inline TIteratorType findImpl(const TKey& key) const noexcept
    {
        CHHT_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getEmpty(), key));
        if (empty())
        {
            return IteratorHelper<TIteratorType>::end(*this);
        }

        CHHT_ASSERT(numChunks > 0);

        const uint64_t hashValue = TKeyInfo::hash(key);
        const ItemAddr addr = hashToEntryPointAddress(hashValue, (numChunks - 1));
        size_t chunkSubIndex = addr.subIndex;
        size_t chunkIndex = addr.chunk;

        CHHT_ASSERT(chunkIndex < numChunks);
        Chunk* CHHT_RESTRICT entryChunk = &chunksStorage[chunkIndex];
        Chunk* CHHT_RESTRICT chunk = entryChunk;

        const size_t maxProbeLength = entryChunk->header.meta.maxProbeLength;
        const size_t _numChunks = numChunks;

        for (size_t probe = 0;;)
        {
            TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[chunkSubIndex]);
            for (; chunkSubIndex < kNumElementsInChunk; chunkSubIndex++, probe++, itemKey++)
            {
                if (TKeyInfo::isEqual(key, *itemKey))
                {
                    return TIteratorType(this, chunkIndex, chunkSubIndex, addr.chunk);
                }

                if (probe > maxProbeLength)
                {
                    return IteratorHelper<TIteratorType>::end(*this);
                }
            }
            chunkSubIndex = 0;
            chunkIndex++;
            chunkIndex = (chunkIndex == _numChunks) ? 0 : chunkIndex;
            CHHT_ASSERT(chunkIndex < numChunks);
            chunk = &chunksStorage[chunkIndex];
        }
    }

    explicit HashTable(uint32_t _numChunks) { create(_numChunks); }

  public:
    class IteratorBase
    {
      protected:
        inline const TValue* getValue() const noexcept
        {
            size_t index = size_t(chunkIndex * size_t(kNumElementsInChunk) + chunkSubIndex);
            TValue* itemValue = reinterpret_cast<TValue*>(&ht->valuesStorage[index]);
            return itemValue;
        }

        inline const TKey* getKey() const noexcept
        {
            CHHT_ASSERT(&ht->chunksStorage[chunkIndex] <= ht->lastChunk);
            CHHT_ASSERT(chunkSubIndex < kNumElementsInChunk);
            return reinterpret_cast<TKey*>(&ht->chunksStorage[chunkIndex].keys[chunkSubIndex]);
        }

      public:
        IteratorBase() = delete;

        IteratorBase(const HashTable* _ht, size_t _chunkIndex, size_t _chunkSubIndex, size_t _entryChunkIndex) noexcept
            : ht(_ht)
            , chunkIndex(_chunkIndex)
            , chunkSubIndex(_chunkSubIndex)
            , entryChunkIndex(_entryChunkIndex)
        {
        }

        bool operator==(const IteratorBase& other) const noexcept
        {
            /* note: we intentionally skip the following check (entryChunkIndex == other.entryChunkIndex) */
            return ht == other.ht && chunkIndex == other.chunkIndex && chunkSubIndex == other.chunkSubIndex;
        }
        bool operator!=(const IteratorBase& other) const noexcept
        {
            /* note: we intentionally skip the following check (entryChunkIndex != other.entryChunkIndex) */
            return ht != other.ht || chunkIndex != other.chunkIndex || chunkSubIndex != other.chunkSubIndex;
        }

        IteratorBase& operator++() noexcept
        {
            size_t _numChunks = ht->numChunks;
            do
            {
                chunkSubIndex++;
                if (chunkSubIndex >= kNumElementsInChunk)
                {
                    chunkIndex++;
                    chunkSubIndex = 0;
                }
            } while (chunkIndex < _numChunks && TKeyInfo::isEqual(TKeyInfo::getEmpty(), *getKey()));

            return *this;
        }

        IteratorBase operator++(int) noexcept
        {
            IteratorBase res = *this;
            ++*this;
            return res;
        }

      protected:
        const HashTable* ht;
        size_t chunkIndex;
        size_t chunkSubIndex;
        size_t entryChunkIndex;

        friend class HashTable<TKey, TValue, TKeyInfo>;
    };

    class IteratorK : public IteratorBase
    {
      public:
        IteratorK() = delete;

        IteratorK(const HashTable* ht, size_t chunkIndex, size_t chunkSubIndex, size_t entryChunkIndex) noexcept
            : IteratorBase(ht, chunkIndex, chunkSubIndex, entryChunkIndex)
        {
        }

        inline const TKey& operator*() const noexcept { return *IteratorBase::getKey(); }
        inline const TKey* operator->() const noexcept { return IteratorBase::getKey(); }
    };

    template <typename TIteratorValue> class TIteratorV : public IteratorBase
    {
      public:
        TIteratorV() = delete;

        TIteratorV(const HashTable* ht, size_t chunkIndex, size_t chunkSubIndex, size_t entryChunkIndex) noexcept
            : IteratorBase(ht, chunkIndex, chunkSubIndex, entryChunkIndex)
        {
        }

        inline TIteratorValue& operator*() const noexcept { return *const_cast<TIteratorValue*>(IteratorBase::getValue()); }
        inline TIteratorValue* operator->() const noexcept { return const_cast<TIteratorValue*>(IteratorBase::getValue()); }
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

            reference(const reference& /*other*/) noexcept = default;
            reference(reference&& /*other*/) noexcept = default;
            reference& operator=(const reference& /*other*/) noexcept = default;
            reference& operator=(reference&& /*other*/) noexcept = default;

            void set(TYPE* _ptr) noexcept { ptr = _ptr; }

            TYPE& get() const noexcept
            {
                CHHT_ASSERT(ptr);
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

        TIteratorKV(const HashTable* ht, size_t chunkIndex, size_t chunkSubIndex, size_t entryChunkIndex) noexcept
            : IteratorBase(ht, chunkIndex, chunkSubIndex, entryChunkIndex)
            , tmpKv(reference<const TKey>(nullptr), reference<TIteratorValue>(nullptr))
        {
        }

        inline const TKey& key() const noexcept { return *IteratorBase::getKey(); }
        inline TIteratorValue& value() const noexcept { return *const_cast<TIteratorValue*>(IteratorBase::getValue()); }

        inline KeyValue& operator*() const noexcept
        {
            updateTmpKV();
            return tmpKv;
        }

        inline KeyValue* operator->() const noexcept
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
        : chunksStorage(nullptr)
        , lastChunk(nullptr)
        , valuesStorage(nullptr)
        , numChunks(0)
        , numElements(0)
    {
    }

    ~HashTable()
    {
        if constexpr (!std::is_trivially_destructible<TValue>::value || !std::is_trivially_destructible<TKey>::value)
        {
            destroy();
        }

        // it looks like calling `free(nullptr)` could be quite expensive for some memory allocators
        if (chunksStorage)
        {
            CHHT_FREE(chunksStorage);
        }
    }

    inline void swap(HashTable& other) noexcept
    {
        std::swap(chunksStorage, other.chunksStorage);
        std::swap(lastChunk, other.lastChunk);
        std::swap(valuesStorage, other.valuesStorage);
        std::swap(numChunks, other.numChunks);
        std::swap(numElements, other.numElements);
    }

    inline void clear()
    {
        if (empty())
        {
            return;
        }

        Chunk* const CHHT_RESTRICT lastValidChunk = lastChunk;
        Chunk* CHHT_RESTRICT chunk = &chunksStorage[0];
        TValue* CHHT_RESTRICT itemValue = nullptr;
        if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
        {
            itemValue = reinterpret_cast<TValue*>(&valuesStorage[0]);
        }

        for (; chunk <= lastValidChunk; chunk++)
        {
            TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[0]);
            TKey* CHHT_RESTRICT endItemKey = reinterpret_cast<TKey*>(&chunk->keys[kNumElementsInChunk]);
            chunk->header.fc.meta = 0;
            for (; itemKey < endItemKey; itemValue++, itemKey++)
            {
                if (!TKeyInfo::isEqual(TKeyInfo::getEmpty(), *itemKey))
                {
                    // destroy value
                    if constexpr ((!std::is_trivially_destructible<TValue>::value) &&
                                  (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value))
                    {
                        destruct(itemValue);
                    }

                    // overwrite key with empty key
                    *itemKey = TKeyInfo::getEmpty();
                }
            }
        }

        // TODO: shrink if needed?
        numElements = 0;
    }

    template <typename TK, class... Args> inline std::pair<TValue*, bool> emplace(TK&& key, Args&&... args)
    {
        static_assert(std::is_same<TKey, typename std::remove_const<typename std::remove_reference<TK>::type>::type>::value,
                      "Expected unversal reference of TKey type");

        CHHT_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getEmpty(), key));

        rehash_if_need();

        CHHT_ASSERT(numChunks > 0);

        const uint64_t hashValue = TKeyInfo::hash(key);
        const size_t maxProbeLength = 0xffff; // due to use of uint16_t in chunk header probe length cannot be greater that 65535
        const size_t _numChunks = numChunks;

        const ItemAddr addr = hashToEntryPointAddress(hashValue, (numChunks - 1));
        size_t chunkSubIndex = addr.subIndex;
        size_t chunkIndex = addr.chunk;

        CHHT_ASSERT(chunkIndex < numChunks);
        Chunk* CHHT_RESTRICT entryChunk = &chunksStorage[chunkIndex];
        Chunk* CHHT_RESTRICT chunk = entryChunk;

        for (size_t probe = 0;;)
        {
            TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[chunkSubIndex]);

            // TODO: Perf
            // I don't need chunkSubIndex at all, I could use for(; itemKey < lastItemKeyInChunk; itemKey++) instead
            for (; chunkSubIndex < kNumElementsInChunk; chunkSubIndex++, probe++, itemKey++)
            {
                CHHT_ASSERT(probe < 0xffff);
                if (TKeyInfo::isEqual(TKeyInfo::getEmpty(), *itemKey))
                {
                    *itemKey = std::move(key);
                    numElements++;
                    entryChunk->header.meta.numEntries++;
                    uint16_t maxProbes = chunk->header.meta.maxProbeLength;
                    entryChunk->header.meta.maxProbeLength = std::max(maxProbes, uint16_t(probe));

                    if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
                    {
                        size_t itemIndex = (chunkIndex * kNumElementsInChunk + chunkSubIndex);
                        CHHT_ASSERT(itemIndex < capacity());
                        TValue* itemValue = reinterpret_cast<TValue*>(&valuesStorage[itemIndex]);
                        construct<TValue>(itemValue, std::forward<Args>(args)...);
                        return {{itemValue}, true};
                    }
                    else
                    {
                        return {{nullptr}, true};
                    }
                }

                if (TKeyInfo::isEqual(key, *itemKey))
                {
                    if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
                    {
                        size_t itemIndex = (chunkIndex * kNumElementsInChunk + chunkSubIndex);
                        CHHT_ASSERT(itemIndex < capacity());
                        TValue* itemValue = reinterpret_cast<TValue*>(&valuesStorage[itemIndex]);
                        return {{itemValue}, false};
                    }
                    else
                    {
                        return {{nullptr}, false};
                    }
                }

                if (probe > maxProbeLength)
                {
                    CHHT_ASSERT(false);
                    return {{nullptr}, false};
                }
            }
            chunkSubIndex = 0;
            chunkIndex++;
            chunkIndex = (chunkIndex == _numChunks) ? 0 : chunkIndex;
            CHHT_ASSERT(chunkIndex < numChunks);
            chunk = &chunksStorage[chunkIndex];
        }
    }

    inline ConstIteratorKV find(const TKey& key) const noexcept { return findImpl<ConstIteratorKV>(key); }
    inline IteratorKV find(const TKey& key) noexcept { return findImpl<IteratorKV>(key); }

    inline bool erase(const IteratorBase it)
    {
        if (it == IteratorHelper<IteratorBase>::end(*this))
        {
            return false;
        }

        CHHT_ASSERT(numElements != 0);
        numElements--;

        Chunk* CHHT_RESTRICT chunk = &chunksStorage[it.entryChunkIndex];
        uint16_t numEntries = chunk->header.meta.numEntries;
        CHHT_ASSERT(numEntries != 0);
        numEntries--;
        if (numEntries == 0)
        {
            chunk->header.meta.maxProbeLength = 0;
        }
        chunk->header.meta.numEntries = numEntries;

        if constexpr ((!std::is_trivially_destructible<TValue>::value) &&
                      (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value))
        {
            TValue* itemValue = const_cast<TValue*>(it.getValue());
            destruct(itemValue);
        }

        // overwrite key with empty key
        TKey* itemKey = const_cast<TKey*>(it.getKey());
        *itemKey = TKeyInfo::getEmpty();

        return true;
    }

    inline uint32_t size() const noexcept { return numElements; }

    inline uint32_t capacity() const noexcept { return (numChunks * kNumElementsInChunk); }

    inline bool empty() const noexcept { return (numElements == 0); }

    inline bool has(const TKey& key) const noexcept { return (find(key) != iend()); }

    inline bool erase(const TKey& key)
    {
        auto it = find(key);
        return erase(it);
    }

    inline TValue& operator[](const TKey& key)
    {
        auto it = emplace(key);
        return *it.first;
    }

    IteratorK begin() const { return IteratorHelper<IteratorK>::begin(*this); }
    IteratorK end() const { return IteratorHelper<IteratorK>::end(*this); }

    ConstIteratorV vbegin() const { return IteratorHelper<ConstIteratorV>::begin(*this); }
    ConstIteratorV vend() const { return IteratorHelper<ConstIteratorV>::end(*this); }
    IteratorV vbegin() { return IteratorHelper<IteratorV>::begin(*this); }
    IteratorV vend() { return IteratorHelper<IteratorV>::end(*this); }

    ConstIteratorKV ibegin() const { return IteratorHelper<ConstIteratorKV>::begin(*this); }
    ConstIteratorKV iend() const { return IteratorHelper<ConstIteratorKV>::end(*this); }
    IteratorKV ibegin() { return IteratorHelper<IteratorKV>::begin(*this); }
    IteratorKV iend() { return IteratorHelper<IteratorKV>::end(*this); }

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

    Keys keys() const { return Keys(this); }
    ConstValues values() const { return ConstValues(this); }
    ConstItems items() const { return ConstItems(this); }

    Values values() { return Values(this); }
    Items items() { return Items(this); }

    // copy ctor
    HashTable(const HashTable& other)
    {
        create(other.numChunks);
        copyOrMoveTo(*this, other);
    }

    // copy assignment
    HashTable& operator=(const HashTable& other)
    {
        uint32_t newNumChunks = numChunks;
        HashTable newHash(newNumChunks);
        copyOrMoveTo(newHash, other);
        swap(newHash);
        return *this;
    }

    // move ctor
    HashTable(HashTable&& other)
        : chunksStorage(nullptr)
        , lastChunk(nullptr)
        , valuesStorage(nullptr)
        , numChunks(0)
        , numElements(0)
    {
        swap(other);
    }

    // move assignment
    HashTable& operator=(HashTable&& other)
    {
        swap(other);
        return *this;
    }

  private:
    Chunk* chunksStorage;
    Chunk* lastChunk;
    ValueStorage* valuesStorage;
    uint32_t numChunks;
    uint32_t numElements;
};

} // namespace Excalibur
