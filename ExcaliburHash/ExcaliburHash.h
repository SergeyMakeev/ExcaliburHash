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
    static inline T getEmpty() noexcept;         /*{ static_assert(false, "Please provide a template specialization for type T"); }*/
    static uint64_t hash(const T& key) noexcept; /*{ static_assert(false, "Please provide a template specialization for type T"); }*/
    static bool isEqual(const T& lhs, const T& rhs) noexcept; /*
    {
        static_assert(false, "Please provide a template specialization for type T");
    }
    */
};

template <> struct KeyInfo<int>
{
    static inline int getEmpty() noexcept { return 0x7fffffff; }
    static uint64_t hash(const int& key) noexcept { return key; }
    static bool isEqual(const int& lhs, const int& rhs) noexcept { return lhs == rhs; }
};

template <> struct KeyInfo<uint32_t>
{
    static inline uint32_t getEmpty() noexcept { return 0xffffffff; }
    static uint64_t hash(const uint32_t& key) noexcept { return key; }
    static bool isEqual(const uint32_t& lhs, const uint32_t& rhs) noexcept { return lhs == rhs; }
};

/*
template <> struct KeyInfo<std::string>
{
    static inline std::string getEmpty() noexcept { return std::string(); }
    static uint64_t hash(const std::string& key) noexcept { return std::hash<std::string>{}(key); }
    static bool isEqual(const std::string& lhs, const std::string& rhs) noexcept { return lhs == rhs; }
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

    using ChunkBits_t = uint32_t;
    static inline constexpr uint64_t kNumElementsInChunk = 32;                    // ChunkBits_t = 32 bits
    static inline constexpr uint64_t kNumElementsMod = (kNumElementsInChunk - 1); // 31
    static inline constexpr uint64_t kDivideToNumElements = 5;                    // (X >> 6) = (X / kNumElementsInChunk)

    static inline constexpr uint64_t kMinNumberOfBuckets = kNumElementsInChunk * kMinNumberOfChunks;

    struct ItemAddr
    {
        // chunk index (upper 56 bits)
        size_t chunk;

        // index within chunk (lower 6 bits) (TODO: use smaller data type?)
        size_t subIndex;

        // "global" index = (chunk * kNumElementsInChunk + subIndex)
        size_t globalIndex;
    };

    template <typename T> static inline T shr(T v, T shift) noexcept
    {
        static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value, "Type T should be an integral unsigned type.");
        return (v >> shift);
    }

    /*

    convert hash value to entry point address (chunk + slot)

    */
    static inline ItemAddr hashToEntryPointAddress(uint64_t hashValue, uint64_t chunkMod) noexcept
    {
        // lower 6 bits = internal chunk subIndex
        const size_t subIndex = size_t(hashValue & kNumElementsMod);
        // upper 58 bits = chunk index
        const size_t chunk = size_t(shr(hashValue, kDivideToNumElements) & chunkMod);
        const size_t globalIndex = size_t(chunk * size_t(kNumElementsInChunk) + subIndex);
        return {chunk, subIndex, globalIndex};
    }

    struct Chunk
    {
        ChunkBits_t overflowBits;
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
                        size_t chunkIndex = shr(firstValidIndex, size_t(kDivideToNumElements));
                        size_t chunkSubIndex = size_t(firstValidIndex & kNumElementsMod);
                        return TIterator(&ht, chunkIndex, chunkSubIndex);
                    }
                }
            }
            // hash table is not empty, but there is no entry point found? this should never happen
            CHHT_ASSERT(false);
            return end(ht);
        }

        static TIterator end(const HashTable& ht) noexcept { return TIterator(&ht, ht.chunkMod + 1, 0); }
    };

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

        IteratorBase(const HashTable* _ht, size_t _chunkIndex, size_t _chunkSubIndex) noexcept
            : ht(_ht)
            , chunkIndex(_chunkIndex)
            , chunkSubIndex(_chunkSubIndex)
        {
        }

        bool operator==(const IteratorBase& other) const noexcept
        {
            return ht == other.ht && chunkIndex == other.chunkIndex && chunkSubIndex == other.chunkSubIndex;
        }
        bool operator!=(const IteratorBase& other) const noexcept
        {
            return ht != other.ht || chunkIndex != other.chunkIndex || chunkSubIndex != other.chunkSubIndex;
        }

        IteratorBase& operator++() noexcept
        {
            size_t lastChunkIndex = ht->chunkMod;
            do
            {
                chunkSubIndex++;
                if (chunkSubIndex >= kNumElementsInChunk)
                {
                    chunkIndex++;
                    chunkSubIndex = 0;
                }
            } while (chunkIndex <= lastChunkIndex && TKeyInfo::isEqual(TKeyInfo::getEmpty(), *getKey()));

            return *this;
        }

        IteratorBase operator++(int) noexcept
        {
            IteratorBase res = *this;
            ++*this;
            return res;
        }

      private:
        const HashTable* ht;
        size_t chunkIndex;
        size_t chunkSubIndex;

        friend class HashTable<TKey, TValue, TKeyInfo>;
    };

    class IteratorK : public IteratorBase
    {
      public:
        IteratorK() = delete;

        IteratorK(const HashTable* ht, size_t chunkIndex, size_t chunkSubIndex) noexcept
            : IteratorBase(ht, chunkIndex, chunkSubIndex)
        {
        }

        inline const TKey& operator*() const noexcept { return *IteratorBase::getKey(); }
        inline const TKey* operator->() const noexcept { return IteratorBase::getKey(); }
    };

    class IteratorV : public IteratorBase
    {
      public:
        IteratorV() = delete;

        IteratorV(const HashTable* ht, size_t chunkIndex, size_t chunkSubIndex) noexcept
            : IteratorBase(ht, chunkIndex, chunkSubIndex)
        {
        }

        inline TValue& operator*() const noexcept { return *const_cast<TValue*>(IteratorBase::getValue()); }
        inline TValue* operator->() const noexcept { return const_cast<TValue*>(IteratorBase::getValue()); }
    };

    class IteratorKV : public IteratorBase
    {
      public:
        // pretty much similar to std::reference_wrapper, but support late initialization
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

        using KeyValue = std::pair<const reference<const TKey>, const reference<TValue>>;

      private:
        void updateTmpKV() const noexcept
        {
            const reference<const TKey>& refKey = tmpKv.first;
            const_cast<reference<const TKey>&>(refKey).set(IteratorBase::getKey());
            const reference<TValue>& refVal = tmpKv.second;
            const_cast<reference<TValue>&>(refVal).set(const_cast<TValue*>(IteratorBase::getValue()));
        }

      public:
        IteratorKV() = delete;

        IteratorKV(const HashTable* ht, size_t chunkIndex, size_t chunkSubIndex) noexcept
            : IteratorBase(ht, chunkIndex, chunkSubIndex)
            , tmpKv(reference<const TKey>(nullptr), reference<TValue>(nullptr))
        {
        }

        inline const TKey& key() const noexcept { return *IteratorBase::getKey(); }
        inline TValue& value() const noexcept { return *const_cast<TValue*>(IteratorBase::getValue()); }

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

    HashTable() noexcept
        : chunksStorage(nullptr)
        , lastChunk(nullptr)
        , valuesStorage(nullptr)
        , chunkMod(0)
        , numElements(0)
        , numBuckets(0)
        , maxEmplaceStepsCount(0)
        , maxLookups(0)
    {
    }

    explicit HashTable(uint32_t _numBuckets)
    {
        _numBuckets = (_numBuckets < kMinNumberOfBuckets) ? kMinNumberOfBuckets : _numBuckets;

        // numBuckets has to be power-of-two
        CHHT_ASSERT((_numBuckets & (_numBuckets - 1)) == 0);

        size_t numChunks = shr(size_t(_numBuckets), size_t(kDivideToNumElements));

        size_t numBytesKeys = numChunks * sizeof(Chunk);
        size_t numBytesKeysAligned = align(numBytesKeys, alignof(ValueStorage));

        size_t alignment = alignof(Chunk);
        size_t numBytesValues = 0;
        if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
        {
            numBytesValues = numChunks * sizeof(ValueStorage) * kNumElementsInChunk;
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

        CHHT_ASSERT(numChunks > 0);
        chunkMod = uint32_t(numChunks - 1);
        numBuckets = _numBuckets;
        numElements = 0;

        lastChunk = &chunksStorage[chunkMod];

        // initialize keys
        Chunk* CHHT_RESTRICT chunk = &chunksStorage[0];
        Chunk* const CHHT_RESTRICT lastValidChunk = lastChunk;

        for (; chunk <= lastValidChunk; chunk++)
        {
            chunk->overflowBits = 0;
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

        // note: num buckets is a power of 2
        // fast log2

        unsigned long idx;
#if defined(_MSC_VER) || defined(__ICL) || defined(__INTEL_COMPILER)
        _BitScanReverse(&idx, unsigned long(numBuckets));
#elif (defined(__GNUC__) && ((__GNUC__ >= 4) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4)))) || defined(__clang__) ||                      \
    defined(__MINGW32__) || defined(__CYGWIN__)
        idx = (32 - __builtin_clz(numBuckets));
#else
    #error "Unsupported platform. Please provide BitScanReverse implementation"
#endif

        unsigned long log2 = idx;
        unsigned long desired = (log2 << 1) + shr(log2, 2ul); // log2(numBuckets) * 2.25
        maxLookups = std::max(uint32_t(8), uint32_t(desired));
        maxLookups = std::min(maxLookups, numBuckets);

        maxEmplaceStepsCount = 0;
    }

    // inline uint32_t getMaxEmplaceSteps() const noexcept { return maxEmplaceStepsCount; }

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

        //
        // TODO: DEBUG only?
        //
        // chunksStorage = nullptr;
        // valuesStorage = nullptr;
        // numBuckets = 0;
        // numElements = 0;
    }

    inline void swap(HashTable& other) noexcept
    {
        std::swap(chunksStorage, other.chunksStorage);
        std::swap(lastChunk, other.lastChunk);
        std::swap(valuesStorage, other.valuesStorage);
        std::swap(numBuckets, other.numBuckets);
        std::swap(numElements, other.numElements);
        std::swap(chunkMod, other.chunkMod);
        std::swap(maxLookups, other.maxLookups);
        std::swap(maxEmplaceStepsCount, other.maxEmplaceStepsCount);
    }

    inline void rehash()
    {
        uint32_t newSize = capacity() * 2;
        HashTable newHash(newSize);

        if (!empty())
        {
            Chunk* CHHT_RESTRICT chunk = &chunksStorage[0];
            for (size_t globalIndex = 0; globalIndex < numBuckets;)
            {
                TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[0]);
                for (size_t subIndex = 0; subIndex < kNumElementsInChunk; subIndex++, globalIndex++, itemKey++)
                {
                    if (!TKeyInfo::isEqual(TKeyInfo::getEmpty(), *itemKey))
                    {
                        if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
                        {
                            TValue* itemValue = reinterpret_cast<TValue*>(&valuesStorage[globalIndex]);
                            newHash.emplace(std::move(*itemKey), std::move(*itemValue));
                        }
                        else
                        {
                            newHash.emplace(std::move(*itemKey));
                        }
                    }
                }
                chunk++;
            }
        }

        this->swap(newHash);
    }

    inline void rehash_if_need()
    {
        const uint32_t _numBuckets = capacity();
        // numBucketsThreshold = (numBuckets * 3/4) (but implemented using bit shifts)
        const uint32_t _numBucketsDiv2 = shr(_numBuckets, 1u);
        const uint32_t _numBucketsDiv4 = shr(_numBuckets, 2u);
        const uint32_t numBucketsThreshold = _numBucketsDiv2 + _numBucketsDiv4;

        // if we have too much lookups (more than we estimated) and loadfactor > 1/8 then rehash
        const bool reshashBecauseForTooMuchCollisions = (maxEmplaceStepsCount > maxLookups) && (numElements >= shr(_numBucketsDiv4, 1u));

        // The selected hash function does not seem to do its job very well (performance warning?)
        // CHHT_ASSERT(maxEmplaceStepsCount > maxLookups && !reshashBecauseForTooMuchCollisions);

        const bool reshashBecauseLoadFactorIsTooHigh = (numElements >= numBucketsThreshold);
        if (reshashBecauseLoadFactorIsTooHigh || reshashBecauseForTooMuchCollisions)
        {
            rehash();
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
            chunk->overflowBits = 0;
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

        const uint64_t hashValue = TKeyInfo::hash(key);
        const ItemAddr addr = hashToEntryPointAddress(hashValue, chunkMod);

        size_t startSubIndex = addr.subIndex;
        const size_t chunkIndex = addr.chunk;

        CHHT_ASSERT(chunkMod > 0);
        CHHT_ASSERT(numBuckets > 0 && (numBuckets & (numBuckets - 1)) == 0);
        const size_t numBucketsMod = (numBuckets - 1);

        const size_t startProbe = addr.globalIndex;
        const size_t endProbe = startProbe + numBuckets;

        Chunk* const CHHT_RESTRICT lastValidChunk = lastChunk;
        Chunk* const CHHT_RESTRICT firstValidChunk = &chunksStorage[0];
        Chunk* CHHT_RESTRICT chunk = &chunksStorage[chunkIndex];
        TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[startSubIndex]);
        ChunkBits_t mask = (ChunkBits_t(1) << ChunkBits_t(startSubIndex));
        uint32_t stepsCount = 0;

        for (size_t probe = startProbe; probe < endProbe;)
        {
            ChunkBits_t overflowBitmask = 0x0;
            for (size_t subIndex = startSubIndex; subIndex < kNumElementsInChunk; subIndex++, probe++, itemKey++, stepsCount++)
            {
                CHHT_ASSERT(mask == (ChunkBits_t(1) << ChunkBits_t(subIndex)));

                overflowBitmask |= mask;
                mask = mask << 1;
                if (TKeyInfo::isEqual(TKeyInfo::getEmpty(), *itemKey))
                {
                    *itemKey = std::move(key);
                    chunk->overflowBits |= overflowBitmask;
                    numElements++;
                    maxEmplaceStepsCount = std::max(stepsCount, maxEmplaceStepsCount);

                    if constexpr (!std::is_same<std::nullptr_t, typename std::remove_reference<TValue>::type>::value)
                    {
                        TValue* itemValue = reinterpret_cast<TValue*>(&valuesStorage[probe & numBucketsMod]);
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
                        TValue* itemValue = reinterpret_cast<TValue*>(&valuesStorage[probe & numBucketsMod]);
                        return {{itemValue}, false};
                    }
                    else
                    {
                        return {{nullptr}, false};
                    }
                }
            }

            chunk->overflowBits |= overflowBitmask;
            mask = 1;
            startSubIndex = 0;
            chunk = (chunk == lastValidChunk) ? firstValidChunk : (chunk + 1);
            itemKey = reinterpret_cast<TKey*>(&chunk->keys[startSubIndex]);
        }

        // hash table is 100% full? this should never happen
        CHHT_ASSERT(false);
        return {{nullptr}, false};
    }

    inline const IteratorKV find(const TKey& key) const noexcept
    {
        CHHT_ASSERT(!TKeyInfo::isEqual(TKeyInfo::getEmpty(), key));

        if (empty())
        {
            return IteratorHelper<IteratorKV>::end(*this);
        }

        const uint64_t hashValue = TKeyInfo::hash(key);
        const ItemAddr addr = hashToEntryPointAddress(hashValue, chunkMod);

        size_t startSubIndex = addr.subIndex;
        size_t chunkIndex = addr.chunk;

        CHHT_ASSERT(chunkMod > 0);
        CHHT_ASSERT(numBuckets > 0 && (numBuckets & (numBuckets - 1)) == 0);
        // const size_t numBucketsMod = (numBuckets - 1);

        const size_t startProbe = addr.globalIndex;
        const size_t endProbe = startProbe + size_t(maxEmplaceStepsCount) + 1;

        Chunk* const CHHT_RESTRICT lastValidChunk = lastChunk;
        Chunk* const CHHT_RESTRICT firstValidChunk = &chunksStorage[0];
        Chunk* CHHT_RESTRICT chunk = &chunksStorage[chunkIndex];
        TKey* CHHT_RESTRICT itemKey = reinterpret_cast<TKey*>(&chunk->keys[startSubIndex]);
        ChunkBits_t overflowBitmask = (ChunkBits_t(1) << ChunkBits_t(startSubIndex));
        for (size_t probe = startProbe; probe < endProbe; chunkIndex++)
        {
            const ChunkBits_t overflowBits = chunk->overflowBits;
            for (size_t subIndex = startSubIndex; subIndex < kNumElementsInChunk; subIndex++, probe++, itemKey++)
            {
                if (TKeyInfo::isEqual(key, *itemKey))
                {
                    return IteratorKV(this, chunkIndex, subIndex);
                }

                CHHT_ASSERT(overflowBitmask == (ChunkBits_t(1) << ChunkBits_t(subIndex)));
                if ((overflowBits & overflowBitmask) == 0)
                {
                    return IteratorHelper<IteratorKV>::end(*this);
                }

                overflowBitmask = overflowBitmask << 1;
            }
            startSubIndex = 0;
            overflowBitmask = 1;
            chunk = (chunk == lastValidChunk) ? firstValidChunk : (chunk + 1);
            itemKey = reinterpret_cast<TKey*>(&chunk->keys[startSubIndex]);
        }

        // hash table is 100% full? this should never happen
        CHHT_ASSERT(false);
        return IteratorHelper<IteratorKV>::end(*this);
    }

    inline bool erase(const IteratorBase it)
    {
        if (it == IteratorHelper<IteratorBase>::end(*this))
        {
            return false;
        }

        numElements--;
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

    inline uint32_t capacity() const noexcept { return numBuckets; }

    inline bool empty() const noexcept { return (numElements == 0); }

    inline bool has(const TKey& key) noexcept { return (find(key) != iend()); }

    inline bool erase(const TKey& key)
    {
        auto it = find(key);
        return erase(it);
    }

    struct convertibleToDefaultValue
    {
        operator TValue() const { return TValue(); }
    };

    inline TValue& operator[](const TKey& key)
    {
        auto it = emplace(key, convertibleToDefaultValue());
        return *it.first;
    }

    IteratorK begin() const { return IteratorHelper<IteratorK>::begin(*this); }
    IteratorK end() const { return IteratorHelper<IteratorK>::end(*this); }

    IteratorV vbegin() const { return IteratorHelper<IteratorV>::begin(*this); }
    IteratorV vend() const { return IteratorHelper<IteratorV>::end(*this); }

    IteratorKV ibegin() const { return IteratorHelper<IteratorKV>::begin(*this); }
    IteratorKV iend() const { return IteratorHelper<IteratorKV>::end(*this); }

    struct Keys
    {
        const HashTable* ht;
        Keys(const HashTable* _ht)
            : ht(_ht)
        {
        }
        IteratorK begin() { return IteratorHelper<IteratorK>::begin(*ht); }
        IteratorK end() { return IteratorHelper<IteratorK>::end(*ht); }
    };
    Keys keys() const { return Keys(this); }

    struct Values
    {
        const HashTable* ht;
        Values(const HashTable* _ht)
            : ht(_ht)
        {
        }
        IteratorV begin() { return IteratorHelper<IteratorV>::begin(*ht); }
        IteratorV end() { return IteratorHelper<IteratorV>::end(*ht); }
    };
    Values values() const { return Values(this); }

    struct Items
    {
        const HashTable* ht;
        Items(const HashTable* _ht)
            : ht(_ht)
        {
        }
        IteratorKV begin() { return IteratorHelper<IteratorKV>::begin(*ht); }
        IteratorKV end() { return IteratorHelper<IteratorKV>::end(*ht); }
    };
    Items items() const { return Items(this); }

  private:
    Chunk* chunksStorage;
    Chunk* lastChunk;
    ValueStorage* valuesStorage;
    uint32_t chunkMod;
    uint32_t numElements;
    uint32_t numBuckets;
    uint32_t maxEmplaceStepsCount;
    uint32_t maxLookups;
};

} // namespace Excalibur
