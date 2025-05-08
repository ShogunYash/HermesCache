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
    // Create a key for the cache lookup using the set index and tag
    CacheKey key(setIndex, tag);
    auto& cacheMap = cacheMaps[setIndex];
    auto it = cacheMap.find(key);
    
    // Return a pointer to the cache line if found and valid, otherwise return null
    if (it != cacheMap.end() && it->second.first.valid && it->second.first.state != INVALID) {
        return &(it->second.first);
    }
    return nullptr;
}

void Cache::updateLRU(int setIndex, uint32_t tag, uint64_t cycle) {
    // Create a key for the cache lookup
    CacheKey key(setIndex, tag);
    auto& lruList = lruLists[setIndex];
    auto& cacheMap = cacheMaps[setIndex];
    
    auto it = cacheMap.find(key);
    if (it != cacheMap.end()) {
        // Move the item to the front of the LRU list (most recently used position)
        lruList.erase(it->second.second);  // Remove from current position
        lruList.push_front(key);           // Add to front of list
        
        // Update the cache map entry with the new list position and cycle
        it->second.second = lruList.begin();
        it->second.first.lastUsedCycle = cycle;
    }
}

std::pair<CacheKey, CacheLine*> Cache::findReplacement(int setIndex, uint64_t cycle) {
    auto& lruList = lruLists[setIndex];
    auto& cacheMap = cacheMaps[setIndex];
    
    // If the set isn't full, we don't need to replace anything yet
    if (cacheMap.size() < static_cast<size_t>(E)) {
        // Return a placeholder key with null cache line (indicating space available)
        CacheKey newKey(setIndex, 0);
        return std::make_pair(newKey, nullptr);
    }
    
    // Otherwise, we need to evict the least recently used entry
    if (!lruList.empty()) {
        // Get the least recently used item (back of the LRU list)
        CacheKey victimKey = lruList.back();
        auto it = cacheMap.find(victimKey);
        if (it != cacheMap.end()) {
            // Return the key and a pointer to the cache line to be replaced
            return std::make_pair(victimKey, &it->second.first);
        }
    }
    
    // Safety fallback - should never reach here with proper implementation
    CacheKey fallbackKey(setIndex, 0);
    return std::make_pair(fallbackKey, nullptr);
}

void Cache::insertLine(int setIndex, uint32_t tag, uint64_t cycle, bool isWrite, CacheState initialState) {
    CacheKey key(setIndex, tag);
    auto& lruList = lruLists[setIndex];
    auto& cacheMap = cacheMaps[setIndex];
    
    // If we're updating an existing line, no need to evict anything
    auto it = cacheMap.find(key);
    if (it != cacheMap.end()) {
        // Update existing line's state and timestamp
        it->second.first.valid = true;
        it->second.first.state = initialState;
        it->second.first.lastUsedCycle = cycle;
        
        // Update LRU position
        lruList.erase(it->second.second);
        lruList.push_front(key);
        it->second.second = lruList.begin();
        return;
    }
    
    // If the set is full, evict the LRU (least recently used) item
    if (cacheMap.size() >= static_cast<size_t>(E)) {
        CacheKey victimKey = lruList.back();
        cacheMap.erase(victimKey);  // Remove from map
        lruList.pop_back();         // Remove from LRU list
    }
    
    // Create a new cache line with the provided state and timestamp
    CacheLine newLine;
    newLine.valid = true;
    newLine.tag = tag;
    newLine.state = initialState;
    newLine.lastUsedCycle = cycle;
    
    // Add to front of LRU list (most recently used position)
    lruList.push_front(key);
    
    // Add to map with reference to its position in the LRU list
    cacheMap[key] = {newLine, lruList.begin()};
}

void Cache::accessCache(bool isWrite, uint32_t address, uint64_t cycle, int coreId, Bus& bus, std::vector<Core*>& cores) {
    // Extract set index and tag from address
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    uint64_t haltcycles = 0;
    
    // Try to find the cache line in our hashmap-based cache
    CacheLine* cacheLine = findLine(setIndex, tag);
    
    if (cacheLine != nullptr) {
        // Cache hit handling
        Core *core = cores[coreId];
        
        if (isWrite) {
            // Write hit cases based on MESI protocol
            
            // Case 1: Writing to a SHARED line requires bus access to invalidate other copies
            if (cacheLine->state == SHARED && bus.isbusy) {
                // Bus is busy, must wait
                idleCycles++;
                return;
            }
            else if (cacheLine->state == SHARED && !bus.isbusy) {
                // Bus is free, invalidate other copies and upgrade to MODIFIED
                bus.busUpgrade(coreId, address, cores, s, b);
                cacheLine->state = MODIFIED;
                invalidations++;
                core->execycles += 1;  // One cycle for write
                core->instPtr++;
                updateLRU(setIndex, tag, cycle);
                writeHits++;
            }
            // Case 2: Writing to an EXCLUSIVE line - silent upgrade to MODIFIED
            else if (cacheLine->state == EXCLUSIVE) {
                cacheLine->state = MODIFIED;
                core->execycles += 1;
                core->instPtr++;
                updateLRU(setIndex, tag, cycle);
                writeHits++;
            }
            // Case 3: Writing to a MODIFIED line
            else if (cacheLine->state == MODIFIED) {
                // Line is already modified, just update timestamp
                // In reality we'd need to write back eventually
                cacheLine->state = MODIFIED;
                core->execycles += 1;  // 1 cycle for hit + 100 for writeback
                // haltcycles += 100;
                core->instPtr++;
                // trafficBytes += (1 << b);  // Count traffic from writeback
                // bus.trafficBytes += (1 << b);
                updateLRU(setIndex, tag, cycle);
                writeHits++;
                // writeBacks++;
                // bus.isbusy = true;
                // bus.moreleft = false;
                // bus.freeCycle = cycle + 100;  // Bus busy for writeback
            }
            else {
                // Bus is busy for a MODIFIED line, wait
                idleCycles++;
                return;
            }
        } else {
            // Read hit is simpler - just update stats and LRU
            readHits++;
            core->execycles += 1;  // One cycle for read hit
            core->instPtr++;
            updateLRU(setIndex, tag, cycle);
        }
        core->nextFreeCycle = cycle + haltcycles;
        return;
    }

    // Cache miss handling
    
    // If the bus is busy, we have to wait
    if (bus.isbusy) {
        idleCycles++;
        return;
    }

    // Find a line to replace using our LRU tracking
    std::pair<CacheKey, CacheLine*> replacement = findReplacement(setIndex, cycle);
    CacheLine* victim = replacement.second;
    
    // Handle eviction if needed
    if (victim != nullptr && victim->state != INVALID) {
        evictions++;
        uint32_t victimTag = victim->tag;
        uint32_t victimAddress = (victimTag << (s + b)) | (setIndex << b);
        Core *core = cores[coreId];
        
        // Handle eviction based on MESI state
        switch (victim->state) {
            case MODIFIED:
                // MODIFIED line requires writeback to memory
                writeBacks++;
                haltcycles += 100;  // 100 cycles penalty for writeback
                core->execycles += 100;
                trafficBytes += (1 << b);  // Count traffic for writeback
                bus.trafficBytes += (1 << b);
                bus.isbusy = true;
                bus.coreid = coreId;
                victim->state = INVALID;  // Invalidate the line
                bus.moreleft = true;      // More processing needed
                bus.freeCycle = cycle + 100;
                core->nextFreeCycle = cycle + haltcycles;
                return;  // Return and come back later after writeback
                
            case SHARED:
                // For SHARED lines, check if other caches have copies
                {
                    int sharedCount = 0;
                    Core* lastCore = nullptr;
                    bus.busTransactions++;

                    // Count other cores with this line in SHARED state
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
                    
                    // If only one other cache has the line, it can be upgraded to EXCLUSIVE
                    if (sharedCount == 1 && lastCore != nullptr) {
                        uint32_t otherSetIndex = (victimAddress >> b) & ((1 << s) - 1);
                        uint32_t otherTag = victimAddress >> (s + b);
                        
                        CacheLine* otherLine = lastCore->cache->findLine(otherSetIndex, otherTag);
                        if (otherLine != nullptr && otherLine->state == SHARED) {
                            otherLine->state = EXCLUSIVE;
                        }
                    }
                    victim->state = INVALID;
                    // If multiple caches have copies, they remain in SHARED state
                }
                break;
                
            case EXCLUSIVE:
                // EXCLUSIVE line doesn't need writeback (it's clean)
                victim->state = INVALID;
                break;
                
            default:
                break;
        }
    }

    // Handle the actual miss operation
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
    core->execycles += 1;
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
    core->execycles += 1;
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