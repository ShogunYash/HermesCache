#include "Bus.hh"
#include "Core.hh"

Bus::Bus() : busTransactions(0), invalidations(0), trafficBytes(0), freeCycle(0), setIndex(0), lineIndex(-1) {}

Bus::BusResult Bus::busRd(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b) {
    busTransactions++;
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    BusResult result = NO_DATA;
    
    // Check if any other cache has this line
    for (Core* core : cores) {
        if (core->id == requesterId) continue;
        
        int lineIndex = core->cache->findLine(setIndex, tag);
        if (lineIndex != -1) {
            CacheLine &line = core->cache->sets[setIndex][lineIndex];
            if (line.state != INVALID) {
                // Another cache has this line
                if (line.state == MODIFIED) {
                    // Modified data needs to be written back to memory
                    // and state changes to SHARED
                    result = MODIFIED_DATA;
                } else if(line.state == SHARED) {
                    // EXCLUSIVE or SHARED state
                    result = SHARED_DATA;
                }
                else{
                    result = EXCLUSIVE_DATA;
                }
            }
        }
    }
    
    // Count bus traffic
    trafficBytes += (1 << b);  // One cache line worth of data
    return result;
}

Bus::BusResult Bus::busRdX(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b) {
    busTransactions++;
    
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    BusResult result = NO_DATA;
    
    // Check if any other cache has this line
    for (Core* core : cores) {
        if (core->id == requesterId) continue;
        
        int lineIndex = core->cache->findLine(setIndex, tag);
        if (lineIndex != -1) {
            CacheLine &line = core->cache->sets[setIndex][lineIndex];
            if (line.valid && line.state != INVALID) {
                if (line.state == MODIFIED) {
                    // Need to write back and invalidate
                    result = MODIFIED_DATA;
                } else {
                    // EXCLUSIVE or SHARED
                    result = SHARED_DATA;
                }
                
                // Invalidate the line in other cache
                line.state = INVALID;
                invalidations++;
            }
        }
    }
    
    // Count bus traffic / I dont think we have to count snoop traffic for busRdX in trafficBytes
    // trafficBytes += (1 << b);
    return result;
}

void Bus::busUpgrade(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b) {
    busTransactions++;
    
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    
    // Invalidate copies in other caches
    for (Core* core : cores) {
        if (core->id == requesterId) continue;
        
        int lineIndex = core->cache->findLine(setIndex, tag);
        if (lineIndex != -1) {
            CacheLine &line = core->cache->sets[setIndex][lineIndex];
            if (line.valid) {
                // Invalidate the line
                line.state = INVALID;
                invalidations++;
            }
        }
    }
}