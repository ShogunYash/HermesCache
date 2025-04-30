#ifndef CACHE_H
#define CACHE_H

#pragma once
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <list>

// MESI protocol states
enum CacheState {
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};

struct CacheLine {
    bool valid;
    CacheState state;
    uint32_t tag;
    uint64_t lastUsedCycle;
    
    CacheLine() : valid(false), state(INVALID), tag(0), lastUsedCycle(0) {}
};

// Cache key structure for map lookup
struct CacheKey {
    uint32_t setIndex;
    uint32_t tag;
    
    // Constructor
    CacheKey(uint32_t s, uint32_t t) : setIndex(s), tag(t) {}
    
    // Equality operator for map
    bool operator==(const CacheKey& other) const {
        return setIndex == other.setIndex && tag == other.tag;
    }
};

// Hash function for the CacheKey
namespace std {
    template<>
    struct hash<CacheKey> {
        size_t operator()(const CacheKey& key) const {
            return hash<uint32_t>()(key.setIndex) ^ hash<uint32_t>()(key.tag);
        }
    };
}

class Cache {
public:
    int s, E, b;
    // LRU list to track usage order per set
    typedef std::list<CacheKey> LRUList;
    
    // Cache map: stores cache lines and references to their position in the LRU list
    typedef std::pair<CacheLine, typename LRUList::iterator> CacheEntry;
    typedef std::unordered_map<CacheKey, CacheEntry> CacheMap;
    
    // One LRU list and one map per set
    std::vector<LRUList> lruLists;
    std::vector<CacheMap> cacheMaps;
    
    // Statistics
    uint64_t readHits;
    uint64_t readMisses;
    uint64_t writeHits;
    uint64_t writeMisses;
    uint64_t writeBacks;
    uint64_t idleCycles;
    uint64_t evictions;
    uint64_t trafficBytes;
    uint64_t invalidations;
    
    Cache(int s, int E, int b);
    
    // Core cache operations
    void accessCache(bool isWrite, uint32_t address, uint64_t cycle, int coreId,
                    class Bus& bus, std::vector<class Core*>& cores);
    
    // Map-based cache operations
    CacheLine* findLine(int setIndex, uint32_t tag);
    std::pair<CacheKey, CacheLine*> findReplacement(int setIndex, uint64_t cycle);
    void updateLRU(int setIndex, uint32_t tag, uint64_t cycle);
    void insertLine(int setIndex, uint32_t tag, uint64_t cycle, bool isWrite, CacheState initialState);
    
    // Bus and miss handling operations
    void busupdate(class Bus& bus);
    void handleReadMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, 
                        std::vector<Core*>& cores, uint64_t haltcycles);
    void handleWriteMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, 
                         std::vector<Core*>& cores, uint64_t haltcycles);
};

#endif // CACHE_H
