# ExcaliburHash

[![Actions Status](https://github.com/SergeyMakeev/ExcaliburHash/workflows/build/badge.svg)](https://github.com/SergeyMakeev/ExcaliburHash/actions)
[![codecov](https://codecov.io/gh/SergeyMakeev/ExcaliburHash/branch/main/graph/badge.svg?token=8YKFZPXMEE)](https://codecov.io/gh/SergeyMakeev/ExcaliburHash)
[![Build status](https://ci.appveyor.com/api/projects/status/vsdtgfr4jubgj2hi?svg=true)](https://ci.appveyor.com/project/SergeyMakeev/excaliburhash)
![MIT](https://img.shields.io/badge/license-MIT-blue.svg)

## About

Excalibur Hash is a high-speed hash map and hash set, ideal for performance-critical uses like video games.
Its design focuses on being friendly to the CPU cache, making it very efficient and fast.
It uses an open addressing hash table and manages removed items with a method called tombstones.

Engineered for ease of use, Excalibur Hash, in a vast majority of cases (99%), serves as a seamless, drop-in alternative to std::unordered_map. However, it's important to note that Excalibur Hash does not guarantee stable addressing.
So, if your project needs to hold direct pointers to the keys or values, Excalibur Hash might not work as you expect. This aside, its design and efficiency make it a great choice for applications where speed is crucial.

## Performance

In this section, you can see a performance comparison against a few popular hash table implementations.
This comparison will show their speed and efficiency, helping you understand which hash table might work best for your project.


Unless otherwise stated, all tests were run using the following configuration
```
OS: Windows 11 Pro (22621.2861)
CPU: Intel i9-12900K
RAM: 128Gb 
```

[Performance test repositoty](https://github.com/SergeyMakeev/SimpleHashTest)  


[absl::flat_hash_map](https://github.com/abseil/abseil-cpp/tree/d3f0c70673ed71ba1581702bbbd0aa8865a575d1)  
[boost::unordered_map](https://github.com/boostorg/unordered/tree/33f81fd49039bccd1aa3dfd5a29ef6073b93009c)  
[ska::flat_hash_map](https://github.com/skarupke/flat_hash_map/tree/2c4687431f978f02a3780e24b8b701d22aa32d9c)  
[ska::unordered_map](https://github.com/skarupke/flat_hash_map/tree/2c4687431f978f02a3780e24b8b701d22aa32d9c)  
[folly::F14ValueMap](https://github.com/facebook/folly/tree/0a095b9ad97da342672cad0d982dd21a9551775c)  
[llvm::DenseMap](https://github.com/llvm/llvm-project/tree/072e0aabbc457b8802dcf7b483e3acebfbde1c33)  
[Luau::DenseHashMap](https://github.com/luau-lang/luau/tree/72d8d443431875607fd457a13fe36ea62804d327)  
[tsl::robin_map](https://github.com/Tessil/robin-map/tree/c7595ba0582e832dfd7c3cbd8c6788faf3d88478)  
[google::dense_hash_map](https://github.com/sparsehash/sparsehash/tree/1dffea3d917445d70d33d0c7492919fc4408fe5c)  
[std::unordered_map](https://github.com/microsoft/STL)  


### CtorDtor

Create and immediatly delete 300,000 hash tables on the stack, using a 'heavyweight' object as the value.
This test quickly shows which hash map implementations 'cheat' by creating many key/value pairs in advance.

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test01.png)


### ClearAndInsertSeq

1. Create a hash table
2. Clear the hash table
3. Insert 599,999 sequential values
4. Repeat steps 2-3 (25 times)
5. Destroy the hash table

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test02.png)

### InsertRndClearAndReInsert

1. Create a hash table
2. Insert 1,000,000 unique random int numbers
3. Clear the hash table
4. Reinsert 1,000,000 unique random int numbers into the same cleared map.
5. Destroy the hash table
6. Repeat steps 1-5 (10 times)

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test03.png)


### InsertRndAndRemove

1. Create a hash table
2. Insert 1,000,000 unique random int numbers
3. Remove all of the inserted entries one by one until the map is empty again.
4. Destroy the hash table
5. Repeat steps 1-4 (10 times)

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test04.png)

### CtorSingleEmplaceDtor
1. Create a hash table
2. Insert a single key/value int the hash table
3. Destroy the hash table
4. Repeat steps 1-3 (300,000 times)

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test05.png)


### InsertAccessWithProbability10
1. Create a hash table
2. Insert or increment 1,000,000 values where 10% of keys are duplicates
   (10% of operations will be modifications and 90% will be insertions)
3. Destroy the hash table
4. Repeat steps 1-3 (8 times)

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test06.png)


### InsertAccessWithProbability50
1. Create a hash table
2. Insert or increment 1,000,000 values where 50% of keys are duplicates
   (50% of operations will be modifications and 50% will be insertions)
3. Destroy the hash table
4. Repeat steps 1-3 (8 times)

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test07.png)

### SearchNonExisting
1. Create a hash table
2. Insert 1,000,000 unique random int numbers
3. Search for non existent keys (10,000,000 times)
4. Destroy the hash table

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test08.png)


### SearchExisting
1. Create a hash table
2. Insert 1,000,000 unique random int numbers
3. Search for existent keys (10,000,000 times)
4. Destroy the hash table


![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test09.png)

### ClearAndInsertRnd

1. Create a hash table
2. Insert 1,000,000 unique random int numbers
3. Destroy the hash table
4. Repeat steps 1-3 (25 times)

![Performance comparison](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/Test10.png)

### Summary

Below, you can find all the tests combined into a single table using normalized timings where 1.0 is the fastest implementation, 2.0 is twice as slow as the fastest configuration, and so on.

```
OS: Windows 11 Pro (22621.2861)
CPU: Intel i9-12900K
RAM: 128Gb 
```
![Intel Summary](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/intel_summary.png)


```
OS: Windows 11 Home (22631.2861)
CPU: AMD Ryzen5 2600
RAM: 16Gb 
```
![AMD Summary](https://raw.githubusercontent.com/SergeyMakeev/ExcaliburHash/master/Images/amd_summary.png)


## Usage

Here is the simplest usage example.

```cpp
#include "ExcaliburHash.h"

int main()
{
  // use hash table as a hash map (key/value)
  Excalibur::HashTable<int, int> hashMap;
  hashMap.emplace(13, 6);

  // use hash table as a hash set (key only)
  Excalibur::HashTable<int, nullptr_t> hashSet;
  hashSet.emplace(13);

  return 0;
}
```


More detailed examples can be found in the unit tests folder.
