#include "Bus.hh"
#include "Cache.hh"

Bus::Bus() : trafficBytes(0), invalidations(0) {}

void Bus::busRd(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b, int blockSize) {
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    for (Core* core : cores) {
        if (core->id == requesterId)
            continue;
        int lineIndex = core->cache->findLine(setIndex, tag);
        if (lineIndex != -1) {
            CacheLine &line = core->cache->sets[setIndex][lineIndex];
            if (line.valid && line.state == MODIFIED) {
                // Write back dirty block penalty plus block transfer.
                core->cache->idleCycles += 100;
                trafficBytes += blockSize;
                line.state = SHARED;
            }
            else if (line.valid && line.state == EXCLUSIVE) {
                line.state = SHARED;
            }
            // For SHARED state, no further action needed.
        }
    }
}

void Bus::busRdX(int requesterId, uint32_t address, std::vector<Core*>& cores, int s, int b) {
    uint32_t setIndex = (address >> b) & ((1 << s) - 1);
    uint32_t tag = address >> (s + b);
    for (Core* core : cores) {
        if (core->id == requesterId)
            continue;
        int lineIndex = core->cache->findLine(setIndex, tag);
        if (lineIndex != -1) {
            CacheLine &line = core->cache->sets[setIndex][lineIndex];
            if (line.valid && line.state != INVALID) {
                line.state = INVALID;
                invalidations++;
            }
        }
    }
}
