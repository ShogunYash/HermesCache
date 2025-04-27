#include "Cache.hh"
#include "Bus.hh"
#include "Core.hh"
#include <algorithm>
#include <climits>

Cache::Cache(int s, int E, int b) 
    : s(s), E(E), b(b), 
      readHits(0), readMisses(0), writeHits(0), writeMisses(0), 
      writeBacks(0), idleCycles(0) {
    
    // Initialize cache structure with 2^s sets, each with E lines
    sets.resize(1 << s, std::vector<CacheLine>(E));
}

bool Cache::accessCache(bool isWrite, uint32_t address, uint64_t cycle, int coreId, Bus& bus, std::vector<Core*>& cores) {
    
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    uint64_t haltcyles = 0;
    // Check if the line is in the cache of current core
    int lineIndex = findLine(setIndex, tag);
    
    if (lineIndex != -1) {
        Core *core = cores[coreId];
        // Cache hit
        if (isWrite) {
            writeHits++;
            // If writing to a shared line, need to invalidate other copies
            if (sets[setIndex][lineIndex].state == SHARED) {
                bus.busUpgrade(coreId, address, cores, s, b);
                sets[setIndex][lineIndex].state = MODIFIED;
            }
        } else {
            readHits++;
        }
        
        core->nextFreeCycle = cycle;
        // Update the last used cycle for LRU policy
        updateLineOnHit(setIndex, lineIndex, cycle, isWrite, coreId, bus, cores);  // LRU cycle update
        return true;
    }
    
    // Cache miss
    if (isWrite) {
        writeMisses++;
    } else {
        readMisses++;
    }
    
    // Find a line to replace
    lineIndex = findReplacement(setIndex, cycle);
    CacheLine& victim = sets[setIndex][lineIndex];
    
    // Handle eviction based on victim's state
    if (victim.valid) {
        // Create the full address of the victim line for coherence operations
        uint32_t victimAddress = (victim.tag << (s + b)) | (setIndex << b);
        
        switch (victim.state) {
            case MODIFIED:
                // Need to write back to memory
                writeBacks++;
                idleCycles += 100;  // Write-back penalty
                haltcyles += 100;  // Write-back penalty
                bus.trafficBytes += (1 << b);
                // Add Invalid logic here if needed
                break;
                
            case EXCLUSIVE:
                // No need to notify other caches since we have the only copy
                // No write-back needed since it's clean
                break;
                
            case SHARED:
            // Check if other caches have copies of this line
            {
                int sharedCount = 0;
                Core* lastCore = nullptr;
                
                for (Core* otherCore : cores) {
                    if (otherCore->id == coreId) continue;  // Skip current core
                    
                    uint32_t otherSetIndex = (victimAddress >> b) & ((1 << s) - 1);
                    uint32_t otherTag = victimAddress >> (s + b);
                    
                    int otherLineIndex = otherCore->cache->findLine(otherSetIndex, otherTag);
                    if (otherLineIndex != -1) {
                        CacheLine& otherLine = otherCore->cache->sets[otherSetIndex][otherLineIndex];
                        if (otherLine.valid && (otherLine.state == SHARED || otherLine.state == EXCLUSIVE)) {
                            sharedCount++;
                            lastCore = otherCore;
                        }
                    }
                }
                
                // If only one other cache has this line, upgrade it to EXCLUSIVE
                if (sharedCount == 1 && lastCore != nullptr) {
                    uint32_t otherSetIndex = (victimAddress >> b) & ((1 << s) - 1);
                    uint32_t otherTag = victimAddress >> (s + b);
                    
                    int otherLineIndex = lastCore->cache->findLine(otherSetIndex, otherTag);
                    if (otherLineIndex != -1) {
                        CacheLine& otherLine = lastCore->cache->sets[otherSetIndex][otherLineIndex];
                        if (otherLine.state == SHARED) {
                            otherLine.state = EXCLUSIVE;
                        }
                    }
                }
                // If multiple caches have the line, they remain in SHARED state
            }
            break;
            
        case INVALID:
                // Nothing to do
                break;
        }
    }
    
    // Fetch data from memory or other caches for read case only
    CacheState initialState = INVALID;  // Default for read miss with no other copies
    
    if (isWrite) {
        // Write miss - get exclusive ownership
        Bus::BusResult res = bus.busRdX(coreId, address, cores, s, b);
        if (res == Bus::MODIFIED_DATA) {
            // Need to wait for writeback
            haltcyles += 100;
            idleCycles += 100;
        }
        initialState = MODIFIED;
    } else {
        // Read miss
        Bus::BusResult res = bus.busRd(coreId, address, cores, s, b);
        if (res == Bus::SHARED_DATA) {
            // Another cache has the line in shared state
            initialState = SHARED;
            // To get the block on our cache line we need to wait for the bus to be free and plus 2*N cycles to transfer 
            // the data N words each 4bytes
            // Assuming 2 cycle per word transfer
            idleCycles += 2 * (1 << b) / 4; // 2 cycles per word transfer
            haltcyles += 2 * (1 << b) / 4; // 2 cycles per word transfer
        } else if (res == Bus::MODIFIED_DATA) {
            // Another cache had the line in modified state
            initialState = SHARED;
            // Need to wait for writeback
            idleCycles += 100;
            haltcyles += 100;
        }
    }

    Core *core = cores[coreId];
    // Update the core's next free cycle based on the bus transaction time
    core->nextFreeCycle = cycle + haltcyles;
    // Insert the new line
    // Cycle used for line will be the current cycle plus the time taken to get the data from the bus
    // and the time taken to transfer the data to the cache line
    insertLine(setIndex, lineIndex, tag, cycle + haltcyles, isWrite, initialState);
    
    return false;
}

int Cache::findLine(int setIndex, uint32_t tag) {
    for (int i = 0; i < E; i++) {
        if (sets[setIndex][i].valid && sets[setIndex][i].tag == tag)
            return i;
    }
    return -1;
}

int Cache::findReplacement(int setIndex, uint64_t cycle) {
    // First, check for invalid lines
    for (int i = 0; i < E; i++) {
        if (!sets[setIndex][i].valid)
            return i;
    }
    
    // Otherwise, use LRU policy
    int lru_index = 0;
    uint64_t lru_cycle = UINT64_MAX;
    
    for (int i = 0; i < E; i++) {
        if (sets[setIndex][i].lastUsedCycle < lru_cycle) {
            lru_cycle = sets[setIndex][i].lastUsedCycle;
            lru_index = i;
        }
    }
    
    return lru_index;
}

// Since the hit handling is now done directly in accessCache, 
// we can simplify or remove these functions

void Cache::updateLineOnHit(int setIndex, int lineIndex, uint64_t cycle, bool isWrite, 
    int coreId, Bus& bus, std::vector<Core*>& cores) {
    // This functionality is now handled in accessCache
    sets[setIndex][lineIndex].lastUsedCycle = cycle;
}

void Cache::insertLine(int setIndex, int lineIndex, uint32_t tag, uint64_t cycle,
                     bool isWrite, CacheState initialState) {
    
    sets[setIndex][lineIndex].valid = true;
    sets[setIndex][lineIndex].tag = tag;
    sets[setIndex][lineIndex].lastUsedCycle = cycle;
    sets[setIndex][lineIndex].state = initialState;
}