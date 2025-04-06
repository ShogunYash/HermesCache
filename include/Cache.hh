#ifndef CACHE_H
#define CACHE_H

#include <vector>
#include <cstdint>
#include <climits>

// MESI states
enum MESI {
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};

// CacheLine represents a single line of the cache.
class CacheLine {
public:
    bool valid;         // true if data is valid
    uint32_t tag;       // tag extracted from the memory address
    MESI state;         // MESI state of the cache line
    int lastUsedCycle;  // for LRU replacement

    CacheLine();
};

// The Cache class encapsulates an L1 cache’s data structure and statistics.
class Cache {
private:
    int s;              // number of set index bits; number of sets = 2^s
    int E;              // associativity (lines per set)
    int b;              // number of block offset bits (block size = 2^b)
    int numSets;        // actual number of sets
    int blockSize;      // block size in bytes
public:
    // 2D vector: each set contains E CacheLine objects
    std::vector<std::vector<CacheLine>> sets;

    // Statistics
    uint64_t accesses;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writeBacks;
    uint64_t idleCycles;
    uint64_t readCount;
    uint64_t writeCount;
    uint64_t totalCycles;

    Cache(int s, int E, int b);
    ~Cache();

    // Returns set index and tag given a 32-bit address.
    void getAddressParts(uint32_t addr, uint32_t &setIndex, uint32_t &tag);

    // Searches for a valid line matching the tag in the specified set.
    int findLine(int setIndex, uint32_t tag);

    // Chooses a replacement candidate within the set (LRU policy).
    int findReplacementCandidate(int setIndex);

    // Update line’s LRU and, for writes, update state.
    void updateLineOnHit(int setIndex, int lineIndex, int cycle, bool isWrite);
};

#endif // CACHE_H
