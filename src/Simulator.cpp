#include "Simulator.hh"
#include "Cache.hh"
#include <iostream>
#include <fstream>
#include <climits>
#include <cstdlib>

Simulator::Simulator(int s, int E, int b)
    : s(s), E(E), b(b), globalCycle(0)
{
    // Create 4 cores.
    for (int i = 0; i < 4; i++) {
        Cache* cache = new Cache(s, E, b);
        Core* core = new Core(i, cache);
        cores.push_back(core);
    }
}

Simulator::~Simulator() {
    for (Core* core : cores) {
        delete core->cache;
        delete core;
    }
}

void Simulator::loadTraces(const std::string& baseName) {
    for (int i = 0; i < 4; i++) {
        std::string filename = baseName + "_proc" + std::to_string(i) + ".trace";
        cores[i]->loadTrace(filename);
    }
}

void Simulator::run() {
    bool pending = true;
    while (pending) {
        pending = false;
        for (Core* core : cores) {
            if (core->instPtr < core->trace.size() &&
                core->nextFreeCycle <= globalCycle) {

                pending = true;
                Request req = core->trace[core->instPtr];
                core->instPtr++;
                core->cache->accesses++;
                if (!req.isWrite)
                    core->cache->readCount++;
                else
                    core->cache->writeCount++;

                uint32_t setIndex, tag;
                core->cache->getAddressParts(req.address, setIndex, tag);
                int lineIndex = core->cache->findLine(setIndex, tag);
                bool hit = (lineIndex != -1 &&
                            core->cache->sets[setIndex][lineIndex].state != INVALID);

                if (hit) {
                    // Cache hit cost is 1 cycle.
                    globalCycle++;
                    core->cache->updateLineOnHit(setIndex, lineIndex, globalCycle, req.isWrite);
                    // For a write hit on a SHARED line, send BusRdX to invalidate other copies.
                    if (req.isWrite && core->cache->sets[setIndex][lineIndex].state == SHARED) {
                        bus.busRdX(core->id, req.address, cores, s, b);
                        core->cache->sets[setIndex][lineIndex].state = MODIFIED;
                    }
                }
                else {
                    // Cache miss.
                    core->cache->misses++;
                    if (!req.isWrite)
                        bus.busRd(core->id, req.address, cores, s, b, (1 << b));
                    else
                        bus.busRdX(core->id, req.address, cores, s, b);

                    int victimIndex = core->cache->findReplacementCandidate(setIndex);
                    CacheLine &victim = core->cache->sets[setIndex][victimIndex];
                    if (victim.valid && victim.state == MODIFIED) {
                        core->cache->writeBacks++;
                        core->cache->idleCycles += 100;  // Write-back penalty.
                        bus.trafficBytes += (1 << b);
                    }
                    if (victim.valid)
                        core->cache->evictions++;

                    // Fetch block from memory (100 cycle penalty).
                    core->cache->idleCycles += 100;
                    bus.trafficBytes += (1 << b);

                    victim.valid = true;
                    victim.tag = tag;
                    victim.lastUsedCycle = globalCycle + 100;
                    if (!req.isWrite) {
                        bool shared = false;
                        for (Core* other : cores) {
                            if (other->id == core->id)
                                continue;
                            int idx = other->cache->findLine(setIndex, tag);
                            if (idx != -1 &&
                                other->cache->sets[setIndex][idx].state != INVALID) {
                                shared = true;
                                break;
                            }
                        }
                        victim.state = shared ? SHARED : EXCLUSIVE;
                    }
                    else {
                        victim.state = MODIFIED;
                    }
                    globalCycle += (1 + 100);
                }
            }
        }
        // If no core could process an instruction this cycle, advance globalCycle.
        if (!pending) {
            uint64_t nextCycle = UINT64_MAX;
            for (Core* core : cores) {
                if (core->instPtr < core->trace.size() &&
                    core->nextFreeCycle < nextCycle)
                    nextCycle = core->nextFreeCycle;
            }
            if (nextCycle != UINT64_MAX)
                globalCycle = nextCycle;
        }
    }

    // Set total cycles for each core.
    for (Core* core : cores) {
        core->cache->totalCycles = globalCycle;
    }
}

void Simulator::printResults(const std::string& outFilename) {
    std::ostream *out;
    std::ofstream ofs;
    if (!outFilename.empty()) {
        ofs.open(outFilename);
        if (!ofs.is_open()) {
            std::cerr << "Error opening output file: " << outFilename << std::endl;
            out = &std::cout;
        }
        else {
            out = &ofs;
        }
    }
    else
        out = &std::cout;

    for (Core* core : cores) {
        *out << "---------- Core " << core->id << " ----------" << std::endl;
        *out << "Read instructions : " << core->cache->readCount << std::endl;
        *out << "Write instructions: " << core->cache->writeCount << std::endl;
        *out << "Total accesses    : " << core->cache->accesses << std::endl;
        double missRate = (core->cache->accesses > 0) ?
                          (double)(core->cache->misses) * 100.0 / core->cache->accesses : 0.0;
        *out << "Cache misses      : " << core->cache->misses << " (" << missRate << "%)" << std::endl;
        *out << "Evictions         : " << core->cache->evictions << std::endl;
        *out << "Writebacks        : " << core->cache->writeBacks << std::endl;
        *out << "Idle cycles       : " << core->cache->idleCycles << std::endl;
        *out << "Total cycles      : " << core->cache->totalCycles << std::endl;
        *out << std::endl;
    }
    *out << "Global bus invalidations: " << bus.invalidations << std::endl;
    *out << "Global bus traffic (bytes): " << bus.trafficBytes << std::endl;

    if (ofs.is_open())
        ofs.close();
}
