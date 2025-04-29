#ifndef CACHE_H
#define CACHE_H

#pragma once
#include <vector>
#include <cstdint>

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

class Cache {
public:
    int s, E, b;
    std::vector<std::vector<CacheLine>> sets;
    
    // Statistics
    uint64_t readHits;
    uint64_t readMisses;
    uint64_t writeHits;
    uint64_t writeMisses;
    uint64_t writeBacks;
    uint64_t idleCycles;  // Cycles spent servicing misses
    
    Cache(int s, int E, int b);
    
    // Core cache operations
    void accessCache(bool isWrite, uint32_t address, uint64_t cycle, int coreId,
                    class Bus& bus, std::vector<class Core*>& cores);
    int findLine(int setIndex, uint32_t tag);
    int findReplacement(int setIndex, uint64_t cycle);
    
    // Updated MESI protocol operations
    void updateLineOnHit(uint32_t setIndex, int lineIndex, uint64_t cycle);
    void insertLine(int setIndex, int lineIndex, uint32_t tag, uint64_t cycle, 
                   bool isWrite, CacheState initialState);
    void busupdate(class Bus& bus);
    void handleReadMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, std::vector<Core*>& cores, uint64_t haltcycles);
    void handleWriteMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, std::vector<Core*>& cores, uint64_t haltcycles);
};

#endif // CACHE_H
