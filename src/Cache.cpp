#include "Cache.hh"
#include "Bus.hh"
#include "Core.hh"
#include <algorithm>
#include <climits>

Cache::Cache(int s, int E, int b) 
    : s(s), E(E), b(b), 
      readHits(0), readMisses(0), writeHits(0), writeMisses(0), 
      writeBacks(0), idleCycles(0), evictions(0), trafficBytes(0), invalidations(0) {
    
    // Initialize the map-based cache structure
    // Create one LRU list and one map for each set
    int numSets = (1 << s);
    lruLists.resize(numSets);
    cacheMaps.resize(numSets);
}

CacheLine* Cache::findLine(int setIndex, uint32_t tag) {
    CacheKey key(setIndex, tag);
    auto& cacheMap = cacheMaps[setIndex];
    auto it = cacheMap.find(key);
    
    if (it != cacheMap.end() && it->second.first.valid && it->second.first.state != INVALID) {
        return &(it->second.first);
    }
    return nullptr;
}

void Cache::updateLRU(int setIndex, uint32_t tag, uint64_t cycle) {
    CacheKey key(setIndex, tag);
    auto& lruList = lruLists[setIndex];
    auto& cacheMap = cacheMaps[setIndex];
    
    auto it = cacheMap.find(key);
    if (it != cacheMap.end()) {
        // Remove from current position in LRU list
        lruList.erase(it->second.second);
        // Add to front of LRU list (most recently used)
        lruList.push_front(key);
        // Update the map with new iterator and cycle
        it->second.second = lruList.begin();
        it->second.first.lastUsedCycle = cycle;
    }
}

std::pair<CacheKey, CacheLine*> Cache::findReplacement(int setIndex, uint64_t cycle) {
    auto& lruList = lruLists[setIndex];
    auto& cacheMap = cacheMaps[setIndex];
    
    // Fix sign comparison warning by casting E to the same type as size()
    if (cacheMap.size() < static_cast<size_t>(E)) {
        // Return a placeholder key - the actual tag will be filled in by the caller
        CacheKey newKey(setIndex, 0);
        return std::make_pair(newKey, nullptr);
    }
    
    // Otherwise, evict the least recently used entry
    if (!lruList.empty()) {
        CacheKey victimKey = lruList.back();
        auto it = cacheMap.find(victimKey);
        if (it != cacheMap.end()) {
            return std::make_pair(victimKey, &it->second.first);
        }
    }
    
    // Fallback - shouldn't reach here if implementation is correct
    CacheKey fallbackKey(setIndex, 0);
    return std::make_pair(fallbackKey, nullptr);
}

void Cache::insertLine(int setIndex, uint32_t tag, uint64_t cycle, bool isWrite, CacheState initialState) {
    CacheKey key(setIndex, tag);
    auto& lruList = lruLists[setIndex];
    auto& cacheMap = cacheMaps[setIndex];
    
    // If we already have this line, update it
    auto it = cacheMap.find(key);
    if (it != cacheMap.end()) {
        // Update existing line
        it->second.first.valid = true;
        it->second.first.state = initialState;
        it->second.first.lastUsedCycle = cycle;
        
        // Update LRU
        lruList.erase(it->second.second);
        lruList.push_front(key);
        it->second.second = lruList.begin();
        return;
    }
    
    // Fix sign comparison warning by casting E to the same type as size()
    if (cacheMap.size() >= static_cast<size_t>(E)) {
        CacheKey victimKey = lruList.back();
        cacheMap.erase(victimKey);
        lruList.pop_back();
    }
    
    // Create new cache line
    CacheLine newLine;
    newLine.valid = true;
    newLine.tag = tag;
    newLine.state = initialState;
    newLine.lastUsedCycle = cycle;
    
    // Add to front of LRU list
    lruList.push_front(key);
    
    // Add to map with reference to LRU position
    cacheMap[key] = {newLine, lruList.begin()};
}

void Cache::accessCache(bool isWrite, uint32_t address, uint64_t cycle, int coreId, Bus& bus, std::vector<Core*>& cores) {
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    uint64_t haltcycles = 0;
    
    // Check if the line is in the cache
    CacheLine* cacheLine = findLine(setIndex, tag);
    
    if (cacheLine != nullptr) {
        Core *core = cores[coreId];
        // Cache hit
        if (isWrite) {
            // If writing to a shared line, need to invalidate other copies
            if (cacheLine->state == SHARED && bus.isbusy) {
                idleCycles++;
                return; // Wait for bus to be free
            }
            else if (cacheLine->state == SHARED && !bus.isbusy) {
                bus.busUpgrade(coreId, address, cores, s, b);
                cacheLine->state = MODIFIED;
                invalidations++;
                core->execycles += 1;
                core->instPtr++;
                updateLRU(setIndex, tag, cycle);
                writeHits++;
            }
            else if (cacheLine->state == EXCLUSIVE) {
                cacheLine->state = MODIFIED;
                core->execycles += 1;
                core->instPtr++;
                updateLRU(setIndex, tag, cycle);
                writeHits++;
            }
            else if (!bus.isbusy && cacheLine->state == MODIFIED) {
                cacheLine->state = MODIFIED;
                core->execycles += 101;
                core->instPtr++;
                trafficBytes += (1 << b);
                bus.trafficBytes += (1 << b);
                updateLRU(setIndex, tag, cycle);
                writeHits++;
                writeBacks++;
                bus.isbusy = true;
                bus.moreleft = false;
                bus.freeCycle = cycle + 100;
            }
            else {
                idleCycles++;
                return;
            }
        } else {
            readHits++;
            core->execycles += 1;
            core->instPtr++;
            updateLRU(setIndex, tag, cycle);
        }
        core->nextFreeCycle = cycle;
        return;
    }

    if (bus.isbusy) {
        idleCycles++;
        return;
    }

    // Replace C++17 structured binding with C++11 compatible code
    std::pair<CacheKey, CacheLine*> replacement = findReplacement(setIndex, cycle);
    // Remove the unused variable victimKey
    CacheLine* victim = replacement.second;
    
    // Handle eviction if needed
    if (victim != nullptr && victim->state != INVALID) {
        evictions++;
        uint32_t victimTag = victim->tag;
        uint32_t victimAddress = (victimTag << (s + b)) | (setIndex << b);
        Core *core = cores[coreId];
        
        switch (victim->state) {
            case MODIFIED:
                writeBacks++;
                haltcycles += 100;
                core->execycles += 100;
                trafficBytes += (1 << b);
                bus.trafficBytes += (1 << b);
                bus.isbusy = true;
                bus.coreid = coreId;
                victim->state = INVALID;
                bus.moreleft = true;
                bus.freeCycle = cycle + 100;
                core->nextFreeCycle = cycle + haltcycles;
                return;
                
            case SHARED:
                // Handle shared state
                {
                    int sharedCount = 0;
                    Core* lastCore = nullptr;
                    bus.busTransactions++;

                    for (Core* otherCore : cores) {
                        if (otherCore->id == coreId) continue;
                        
                        uint32_t otherSetIndex = (victimAddress >> b) & ((1 << s) - 1);
                        uint32_t otherTag = victimAddress >> (s + b);
                        
                        CacheLine* otherLine = otherCore->cache->findLine(otherSetIndex, otherTag);
                        if (otherLine != nullptr && otherLine->state == SHARED) {
                            sharedCount++;
                            lastCore = otherCore;
                        }
                    }
                    
                    // If only one other cache has this line, upgrade it
                    if (sharedCount == 1 && lastCore != nullptr) {
                        uint32_t otherSetIndex = (victimAddress >> b) & ((1 << s) - 1);
                        uint32_t otherTag = victimAddress >> (s + b);
                        
                        CacheLine* otherLine = lastCore->cache->findLine(otherSetIndex, otherTag);
                        if (otherLine != nullptr && otherLine->state == SHARED) {
                            otherLine->state = EXCLUSIVE;
                        }
                    }
                }
                break;
                
            case EXCLUSIVE:
                // No write-back needed
                break;
                
            default:
                break;
        }
    }

    // Handle miss
    if (!isWrite) {
        handleReadMiss(coreId, address, cycle, bus, cores, haltcycles);
    } else {
        handleWriteMiss(coreId, address, cycle, bus, cores, haltcycles);
    }
}

void Cache::handleReadMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, std::vector<Core*>& cores, uint64_t haltcycles) {
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    CacheState finalState = INVALID;
    
    Bus::BusResult res = bus.busRd(coreId, address, cores, s, b);
    
    if (res == Bus::SHARED_DATA || res == Bus::EXCLUSIVE_DATA) {
        finalState = SHARED;
        // Find other cores with this line
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            CacheLine* line = core->cache->findLine(setIndex, tag);
            if (line != nullptr && (line->state == SHARED || line->state == EXCLUSIVE)) {
                line->state = SHARED;
                core->cache->trafficBytes += (1 << b);
                break;
            }
        }
        
        Core *core = cores[coreId];
        core->execycles += (2 * (1 << b) / 4);
        haltcycles += (2 * (1 << b) / 4);
        bus.isbusy = true;
        bus.freeCycle = cycle + haltcycles;
        bus.trafficBytes += (1 << b);
        trafficBytes += (1 << b);
    } 
    else if (res == Bus::MODIFIED_DATA) {
        finalState = SHARED;
        
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            CacheLine* line = core->cache->findLine(setIndex, tag);
            if (line != nullptr && line->state == MODIFIED) {
                line->state = SHARED;
                core->cache->trafficBytes += (1 << b);
                core->cache->writeBacks++;
                break;
            }
        }
        
        haltcycles += (2 * (1 << b) / 4);
        Core *core = cores[coreId];
        core->execycles += (2 * (1 << b) / 4);
        bus.isbusy = true;
        bus.freeCycle = cycle + haltcycles + 100;
        bus.trafficBytes += (1 << b);
        trafficBytes += (1 << b);
    } 
    else {
        finalState = EXCLUSIVE;
        Core *core = cores[coreId];
        core->execycles += 100;
        haltcycles += 100;
        bus.isbusy = true;
        bus.freeCycle = cycle + haltcycles;
        bus.trafficBytes += (1 << b);
        trafficBytes += (1 << b);
    }

    Core *core = cores[coreId];
    core->nextFreeCycle = cycle + haltcycles;
    readMisses++;
    insertLine(setIndex, tag, cycle + haltcycles, false, finalState);
    core->instPtr++;
}

void Cache::handleWriteMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, std::vector<Core*>& cores, uint64_t haltcycles) {
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    
    Bus::BusResult res = bus.busRd(coreId, address, cores, s, b);
    
    if (res == Bus::SHARED_DATA || res == Bus::EXCLUSIVE_DATA) {
        invalidations++;
        
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            CacheLine* line = core->cache->findLine(setIndex, tag);
            if (line != nullptr && (line->state == SHARED || line->state == EXCLUSIVE)) {
                line->state = INVALID;
            }
        }
    } 
    else if (res == Bus::MODIFIED_DATA) {
        invalidations++;
        
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            CacheLine* line = core->cache->findLine(setIndex, tag);
            if (line != nullptr && line->state == MODIFIED) {
                line->state = INVALID;
                core->cache->writeBacks++;
                core->cache->trafficBytes += (1 << b);
            }
        }
        
        idleCycles += 100;    
        haltcycles += 100;  
        bus.isbusy = true;
        bus.trafficBytes += (1 << b);
    }

    Core *core = cores[coreId];
    bus.freeCycle = cycle + haltcycles;
    haltcycles += 100;
    core->execycles += 100;
    core->nextFreeCycle = cycle + haltcycles;
    trafficBytes += (1 << b);
    
    insertLine(setIndex, tag, cycle + haltcycles, true, MODIFIED);
    writeMisses++;
    core->instPtr++;
}

void Cache::busupdate(class Bus &bus) {
    bus.isbusy = false;
    bus.freeCycle = 0;
    bus.coreid = 0;
    bus.moreleft = false;
}