#include "Cache.hh"
#include "Bus.hh"
#include "Core.hh"
#include <algorithm>
#include <climits>

Cache::Cache(int s, int E, int b) 
    : s(s), E(E), b(b), 
      readHits(0), readMisses(0), writeHits(0), writeMisses(0), 
      writeBacks(0), idleCycles(0), evictions(0) {
    
    // Initialize cache structure with 2^s sets, each with E lines
    sets.resize(1 << s, std::vector<CacheLine>(E));
}

void Cache::accessCache(bool isWrite, uint32_t address, uint64_t cycle, int coreId, Bus& bus, std::vector<Core*>& cores) {
    
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    uint64_t haltcycles = 0;
    // Check if the line is in the cache of current core
    int lineIndex = findLine(setIndex, tag);
    
    if (lineIndex != -1) {
        Core *core = cores[coreId];
        // Cache hit
        if (isWrite) {
            // If writing to a shared line, need to invalidate other copies
            if (sets[setIndex][lineIndex].state == SHARED && !bus.isbusy) {
                bus.busUpgrade(coreId, address, cores, s, b);
                sets[setIndex][lineIndex].state = MODIFIED;
                core->execycles += 1;  // Increment execution cycles for the core
                core->instPtr++;       // Move to the next instruction in the trace
                // Update the last used cycle for LRU policy
                updateLineOnHit(setIndex, lineIndex, cycle);  // LRU cycle update
                writeHits++;
            }
            else if (sets[setIndex][lineIndex].state == EXCLUSIVE) {
                sets[setIndex][lineIndex].state = MODIFIED;
                core->execycles += 1;  // Increment execution cycles for the core
                core->instPtr++;       // Move to the next instruction in the trace
                // Update the last used cycle for LRU policy
                updateLineOnHit(setIndex, lineIndex, cycle);  // LRU cycle update
                writeHits++;
            }
            else {
                // If the line is already in MODIFIED state, just update it
                sets[setIndex][lineIndex].state = MODIFIED;
                core->execycles += 1;  // Increment execution cycles for the core
                core->instPtr++;       // Move to the next instruction in the trace
                // Update the last used cycle for LRU policy
                updateLineOnHit(setIndex, lineIndex, cycle);  // LRU cycle update
                writeHits++;
            }
        } else {
            readHits++;
            core->execycles += 1;  // Increment execution cycles for the core
            core->previnstr = core->instPtr;  // Update the previous instruction pointer
            core->instPtr++;       // Move to the next instruction in the trace
            // Update the last used cycle for LRU policy
            updateLineOnHit(setIndex, lineIndex, cycle);  // LRU cycle update
        }
        core->nextFreeCycle = cycle;    // Update the core's next free cycle
        return;
    }
    
    // Cache miss
    // For first instr it will not take writemisses or read misses
    Core *core = cores[coreId];
    if (isWrite && core->previnstr != core->instPtr) {
        writeMisses++;
    } else if (core->previnstr != core->instPtr) {
        // Read Miss
        readMisses++;
    }
    core->previnstr = core->instPtr;  // Update the previous instruction pointer
    
    if (!isWrite) {
        // Read miss handling
        handleReadMiss(coreId, address, cycle, bus, cores, haltcycles);
    } else {
        // Write miss handling
        handleWriteMiss(coreId, address, cycle, bus, cores, haltcycles);
    }
}


void Cache::handleReadMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, std::vector<Core*>& cores, uint64_t haltcycles ) {
    // Handle read miss logic here
    // victim is present and bus is not free
    // We cant fetch the data from memory or cache
    if (bus.isbusy) {
        // We can't fetch new data
        idleCycles++;
        return;  // Wait for the bus to be free before proceeding
    }
    // Find a line to replace
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    int lineIndex = findReplacement(setIndex, cycle);
    CacheLine& victim = sets[setIndex][lineIndex];

    // Handle eviction based on victim's state
    // Bus is free
    if (victim.state != INVALID) {
        evictions++;
        // Create the full address of the victim line for coherence operations
        uint32_t victimAddress = (victim.tag << (s + b)) | (setIndex << b);
        Core *core = cores[coreId];
        switch (victim.state) {
            case MODIFIED:
                // Need to write back to memory
                writeBacks++;
                idleCycles += 100;  // Write-back penalty
                haltcycles += 100;   // Write-back penalty
                bus.trafficBytes += (1 << b); // Data being written from cache to memory
                bus.isbusy = true;  // Bus is busy during write-back
                // This assumption wrong
                bus.freeCycle = cycle + 100;  // Set the bus free cycle after write-back
                bus.coreid = coreId;  // Set the core ID for the write-back
                // Update the line to invalid
                victim.state = INVALID; 
                bus.moreleft = true;      // more left to update
                core->nextFreeCycle = cycle + haltcycles;
                return;
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
                        if (otherLine.valid && otherLine.state == SHARED ) {
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

            case EXCLUSIVE:
                // No need to notify other caches since we have the only copy
                // No write-back needed since it's clean
                break;
        }
    }

    // Fetch data from memory or other caches for read case only
    CacheState FinalState = INVALID;  // Default for read miss with no other copies
    // Now we need to do bus transactions
    // Bus should be free
    // Write miss now adding new block in our cache
    Bus::BusResult res = bus.busRd(coreId, address, cores, s, b);
    if (res == Bus::SHARED_DATA || res == Bus::EXCLUSIVE_DATA) {
        // Another cache has the line in shared state
        FinalState = SHARED;
        uint32_t tag = address >> (s + b);
        // Find a core to stall that have this line
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            int lineIndex = core->cache->findLine(setIndex, tag);
            if (lineIndex != -1) {
                CacheLine &line = core->cache->sets[setIndex][lineIndex];
                if( line.state == SHARED || line.state == EXCLUSIVE){
                    core->cache->sets[setIndex][lineIndex].state = SHARED;
                    break;
                }
            }
        }
        // What if we first evict the victim line (For Modified) then we come over 
        // To get the block on our cache line we need to wait for the bus to be free and plus 2*N cycles to transfer 
        // the data N words each 4bytes
        // Assuming 2 cycle per word transfer
        idleCycles += (2 * (1 << b) / 4);   // 2 cycles per word transfer
        haltcycles += (2 * (1 << b) / 4);    // 2 cycles per word transfer
    } else if (res == Bus::MODIFIED_DATA) {
        // Another cache had the line in modified state
        // Check for this evictions to take or not
        FinalState = SHARED;
        uint32_t tag = address >> (s + b);
        // Find a core to stall that have this line
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            int lineIndex = core->cache->findLine(setIndex, tag);
            if (lineIndex != -1) {
                CacheLine &line = core->cache->sets[setIndex][lineIndex];
                if( line.state == MODIFIED){
                    core->cache->sets[setIndex][lineIndex].state = SHARED;
                }
            }
        }
        // Need to wait for writeback 
        idleCycles += 100 + (2 * (1 << b) / 4);
        haltcycles += 100 + (2 * (1 << b) / 4);
        bus.isbusy = true;
        bus.freeCycle = cycle + haltcycles;  // Set the bus free cycle after write-back
    } else{
        // No other cache have this block
        FinalState = EXCLUSIVE;
        uint32_t tag = address >> (s + b);
        // Need to wait for writeback 
        idleCycles += 100 ;
        haltcycles += 100 ;
        bus.isbusy = true;
        bus.freeCycle = cycle + haltcycles;  // Set the bus free cycle after write-back
    }

    Core *core = cores[coreId];
    // Update the core's next free cycle based on the bus transaction time
    core->nextFreeCycle = cycle + haltcycles;
    // Insert the new line
    // Cycle used for line will be the current cycle plus the time taken to get the data from the bus
    // and the time taken to transfer the data to the cache line
    uint32_t tag = address >> (s + b);
    insertLine(setIndex, lineIndex, tag, cycle + haltcycles, false, FinalState);
    core->instPtr++;  // Move to the next instruction in the trace
}

void Cache::handleWriteMiss(int coreId, uint64_t address, uint64_t cycle, Bus& bus, std::vector<Core*>& cores, uint64_t haltcycles){
    // Handle write miss logic here
    // We cant fetch the data from memory or cache
    if (bus.isbusy) {
        // We can't fetch new data
        idleCycles++;
        return;  // Wait for the bus to be free before proceeding
    }
    // Find a line to replace
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    int lineIndex = findReplacement(setIndex, cycle);
    CacheLine& victim = sets[setIndex][lineIndex];

    // Handle eviction based on victim's state
    // Bus is free
    if (victim.state != INVALID) {
        evictions++;
        // Create the full address of the victim line for coherence operations
        uint32_t victimAddress = (victim.tag << (s + b)) | (setIndex << b);
        Core *core = cores[coreId];
        switch (victim.state) {
            case MODIFIED:
                // Need to write back to memory
                writeBacks++;
                idleCycles += 100;  // Write-back penalty
                haltcycles += 100;   // Write-back penalty
                bus.trafficBytes += (1 << b); // Data being written from cache to memory
                bus.isbusy = true;  // Bus is busy during write-back
                // This assumption wrong
                bus.freeCycle = cycle + 100;  // Set the bus free cycle after write-back
                bus.coreid = coreId;  // Set the core ID for the write-back
                // Update the line to invalid
                victim.state = INVALID; 
                bus.moreleft = true;      // more left to update
                core->nextFreeCycle = cycle + haltcycles;
                return;
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
                        if (otherLine.valid && otherLine.state == SHARED ) {
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

            case EXCLUSIVE:
                // No need to notify other caches since we have the only copy
                // No write-back needed since it's clean
                break;
        }
    }
    
    // Fetch data from memory
    CacheState FinalState = MODIFIED;  // Default for read miss with no other copies

    // Read miss
    Bus::BusResult res = bus.busRd(coreId, address, cores, s, b);
    if (res == Bus::SHARED_DATA || res == Bus::EXCLUSIVE_DATA) {
        // Another cache has the line in shared state
        uint32_t tag = address >> (s + b);
        // Find a core to stall that have this line
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            int lineIndex = core->cache->findLine(setIndex, tag);
            if (lineIndex != -1) {
                CacheLine &line = core->cache->sets[setIndex][lineIndex];
                if( line.state == SHARED || line.state == EXCLUSIVE){
                    core->cache->sets[setIndex][lineIndex].state = INVALID;
                }
            }
        }
    } else if (res == Bus::MODIFIED_DATA) {
        // Another cache had the line in modified state
        uint32_t tag = address >> (s + b);
        // Find a core to stall that have this line
        for (Core* core : cores) {
            if (core->id == coreId) continue;
            
            int lineIndex = core->cache->findLine(setIndex, tag);
            if (lineIndex != -1) {
                CacheLine &line = core->cache->sets[setIndex][lineIndex];
                if( line.state == MODIFIED){
                    core->cache->sets[setIndex][lineIndex].state = INVALID;
                }
            }
        }
        // Need to wait for writeback 
        idleCycles += 100;
        haltcycles += 100;
        bus.isbusy = true;
    }

    Core *core = cores[coreId];
    // ADD another 100 cycles for reading data from memory to cache
    idleCycles += 100;
    haltcycles += 100;
    // Update the core's next free cycle based on the bus transaction time
    core->nextFreeCycle = cycle + haltcycles;
    // Insert the new line
    // Cycle used for line will be the current cycle plus the time taken to get the data from the bus
    // and the time taken to transfer the data to the cache line
    uint32_t tag = address >> (s + b);
    insertLine(setIndex, lineIndex, tag, cycle + haltcycles, false, FinalState);
    core->instPtr++;  // Move to the next instruction in the trace
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
void Cache::updateLineOnHit(uint32_t setIndex, int lineIndex, uint64_t cycle) {
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

void Cache::busupdate(class Bus &bus){
    // Update the bus now make it free again
    bus.isbusy = false;
    bus.freeCycle = 0; // Reset the bus free cycle
    bus.coreid = 0; // Reset the core ID
    bus.moreleft = false;
}