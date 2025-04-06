#ifndef BUS_H
#define BUS_H

#include <cstdint>
#include <vector>
#include "Core.hh"

// The Bus class handles inter-core transactions to ensure cache coherence.
class Bus {
public:
    uint64_t trafficBytes;   // Total bytes moved on the bus
    uint64_t invalidations;  // Count of invalidation transactions on the bus

    Bus();
    // Bus read transaction (for read misses).
    void busRd(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b, int blockSize);
    // Bus read-exclusive transaction (for write misses).
    void busRdX(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b);
};

#endif // BUS_H
