#include "Cache.hh"

CacheLine::CacheLine() : valid(false), tag(0), state(INVALID), lastUsedCycle(0) {}

Cache::Cache(int s, int E, int b)
    : s(s), E(E), b(b),
      accesses(0), misses(0), evictions(0), writeBacks(0),
      idleCycles(0), readCount(0), writeCount(0), totalCycles(0)
{
    numSets = 1 << s;
    blockSize = 1 << b;
    sets.resize(numSets, std::vector<CacheLine>(E));
}

Cache::~Cache() {
    // Nothing to delete here.
}

void Cache::getAddressParts(uint32_t addr, uint32_t &setIndex, uint32_t &tag) {
    setIndex = (addr >> b) & ((1 << s) - 1);
    tag = addr >> (s + b);
}

int Cache::findLine(int setIndex, uint32_t tag) {
    for (int i = 0; i < E; i++) {
        if (sets[setIndex][i].valid && sets[setIndex][i].tag == tag)
            return i;
    }
    return -1;
}

int Cache::findReplacementCandidate(int setIndex) {
    int candidate = 0;
    int oldestCycle = INT_MAX;
    for (int i = 0; i < E; i++) {
        if (!sets[setIndex][i].valid)
            return i;
        if (sets[setIndex][i].lastUsedCycle < oldestCycle) {
            oldestCycle = sets[setIndex][i].lastUsedCycle;
            candidate = i;
        }
    }
    return candidate;
}

void Cache::updateLineOnHit(int setIndex, int lineIndex, int cycle, bool isWrite) {
    sets[setIndex][lineIndex].lastUsedCycle = cycle;
    if (isWrite) {
        // Upgrade state if access is a write and line is in SHARED or EXCLUSIVE state.
        if (sets[setIndex][lineIndex].state == SHARED ||
            sets[setIndex][lineIndex].state == EXCLUSIVE)
            sets[setIndex][lineIndex].state = MODIFIED;
    }
}
