#ifndef BUS_H
#define BUS_H
#pragma once
#include <vector>
#include <cstdint>
#include "Cache.hh"

class Core;

class Bus {
public:
    Bus();
    
    // Enhanced bus transactions
    enum BusResult {
        NO_DATA,        // No cache has the data
        SHARED_DATA,    // Data found in another cache (shared)
        MODIFIED_DATA   // Data found in modified state (needs writeback)
    };
    
    // Statistics
    uint64_t busTransactions;
    uint64_t invalidations;
    uint64_t trafficBytes;
    bool isbusy;    
    
    // Bus read (for read misses)
    BusResult busRd(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b);
    
    // Bus read exclusive (for write misses or upgrades)
    BusResult busRdX(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b);
    
    // Bus upgrade (for write to shared line)
    void busUpgrade(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b);
    
};

#endif // BUS_H
