#include "Bus.hh"
#include "Core.hh"

Bus::Bus() : busTransactions(0), invalidations(0), trafficBytes(0),  
                isbusy(false), freeCycle(0), moreleft(false), coreid(0) {}

Bus::BusResult Bus::busRd(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b) {
    busTransactions++;  // Increment transactions counter for statistics
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    BusResult result = NO_DATA;
    
    // Check all other cores for the requested line
    for (Core* core : cores) {
        if (core->id == requesterId) continue;  // Skip requesting core
        
        // Use the hashmap-based lookup for efficiency
        CacheLine* line = core->cache->findLine(setIndex, tag);
        if (line != nullptr && line->state != INVALID) {
            // Found the line in another cache
            if (line->state == MODIFIED) {
                // Modified data requires writeback to memory first
                result = MODIFIED_DATA;
            } else if(line->state == SHARED) {
                // Shared data can be supplied directly
                result = SHARED_DATA;
            }
            else {
                // Exclusive data needs to be changed to shared
                result = EXCLUSIVE_DATA;
            }
        }
    }
    
    return result;
}

Bus::BusResult Bus::busRdX(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b) {
    busTransactions++;  // Increment transaction counter
    invalidations++;    // This always causes invalidations in other caches
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    BusResult result = NO_DATA;
    
    // Check other cores for copies of this line
    for (Core* core : cores) {
        if (core->id == requesterId) continue;  // Skip requesting core
        
        // Use the hashmap-based lookup
        CacheLine* line = core->cache->findLine(setIndex, tag);
        if (line != nullptr && line->valid && line->state != INVALID) {
            // Found a copy in another cache
            if (line->state == MODIFIED) {
                // Modified data requires writeback before invalidation
                result = MODIFIED_DATA;
            } else {
                // Shared or Exclusive can be invalidated directly
                result = SHARED_DATA;
            }
            
            // Invalidate the line in the other cache
            line->state = INVALID;
        }
    }
    
    return result;
}

void Bus::busUpgrade(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b) {
    busTransactions++;  // Increment transaction counter
    invalidations++;    // Upgrade always invalidates other copies
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    
    // Invalidate all other copies
    for (Core* core : cores) {
        if (core->id == requesterId) continue;  // Skip requesting core
        
        // Find and invalidate any copies in other caches
        CacheLine* line = core->cache->findLine(setIndex, tag);
        if (line != nullptr && line->state != INVALID) {
            line->state = INVALID;  // Invalidate the line
        }
    }
}