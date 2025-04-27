#include "Simulator.hh"
#include "Cache.hh"
#include <iostream>
#include <fstream>
#include <climits>
#include <cstdlib>

Simulator::Simulator(int s, int E, int b)
    : s(s), E(E), b(b), globalCycle(0), totalCycles(0)
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

// (partial implementation - just the run method)

void Simulator::run() {
    uint64_t globalCycle = 0;
    bool pending = false;

    while (true) {
        pending = false;
        
        // Process each core for the current cycle
        for (Core* core : cores) {
            // Skip if core is waiting for a previous request
            if (core->nextFreeCycle > globalCycle)
                continue;
            
            // Check if core has more instructions to process
            if (core->instPtr < core->trace.size()) {
                pending = true;
                
                // Get the current request
                Request& req = core->trace[core->instPtr];
                
                // Access the cache
                bool hit = core->cache->accessCache(req.isWrite, req.address, globalCycle, core->id, bus, cores);
                
                // Update core state
                core->instPtr++;
                
            }
        }
        
        // If no more instructions and no pending operations, we're done
        if (!pending) {
            bool allDone = true;
            for (Core* core : cores) {
                if (core->instPtr < core->trace.size() || core->nextFreeCycle > globalCycle) {
                    allDone = false;
                    break;
                }
            }
            if (allDone) break;
            
            // Find the next cycle when a core becomes free
            uint64_t nextCycle = UINT64_MAX;
            for (Core* core : cores) {
                if (core->instPtr < core->trace.size() && core->nextFreeCycle < nextCycle)
                    nextCycle = core->nextFreeCycle;
            }
            
            if (nextCycle != UINT64_MAX)
                globalCycle = nextCycle;
            else
                break; // Safety check
        } else {
            // Move to next cycle
            globalCycle++;
        }
    }

    // Store final cycle count
    this->totalCycles = globalCycle;
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
        *out << "Read hits         : " << core->cache->readHits << std::endl;
        *out << "Read misses       : " << core->cache->readMisses << std::endl;
        *out << "Write hits        : " << core->cache->writeHits << std::endl;
        *out << "Write misses      : " << core->cache->writeMisses << std::endl;
        *out << "Total accesses    : " << core->cache->readHits + core->cache->readMisses + 
                                          core->cache->writeHits + core->cache->writeMisses << std::endl;
        double missRate = (core->cache->readMisses + core->cache->writeMisses) * 100.0 / 
                          (core->cache->readHits + core->cache->readMisses + 
                           core->cache->writeHits + core->cache->writeMisses);
        *out << "Miss rate         : " << missRate << "%" << std::endl;
        *out << "Evictions         : " << (core->cache->readMisses + core->cache->writeMisses) - 
                                          core->cache->writeBacks << std::endl;
        *out << "Writebacks        : " << core->cache->writeBacks << std::endl;
        *out << "Idle cycles       : " << core->cache->idleCycles << std::endl;
        *out << std::endl;
    }
    *out << "Total cycles         : " << totalCycles << std::endl;
    *out << "Bus transactions     : " << bus.busTransactions << std::endl;
    *out << "Bus invalidations    : " << bus.invalidations << std::endl;
    *out << "Global bus traffic   : " << bus.trafficBytes << " bytes" << std::endl;

    if (ofs.is_open())
        ofs.close();
}

// void Simulator::run() {
//     bool pending = true;
//     while (pending) {
//         pending = false;
//         for (Core* core : cores) {
//             if (core->instPtr < core->trace.size() &&
//                 core->nextFreeCycle <= globalCycle) {

//                 pending = true;
//                 Request req = core->trace[core->instPtr];
//                 core->instPtr++;
//                 core->cache->accesses++;
//                 if (!req.isWrite)
//                     core->cache->readCount++;
//                 else
//                     core->cache->writeCount++;

//                 uint32_t setIndex, tag;
//                 core->cache->getAddressParts(req.address, setIndex, tag);
//                 int lineIndex = core->cache->findLine(setIndex, tag);
//                 bool hit = (lineIndex != -1 &&
//                             core->cache->sets[setIndex][lineIndex].state != INVALID);

//                 if (hit) {
//                     // Cache hit cost is 1 cycle.
//                     globalCycle++;
//                     core->cache->updateLineOnHit(setIndex, lineIndex, globalCycle, req.isWrite);
//                     // For a write hit on a SHARED line, send BusRdX to invalidate other copies.
//                     if (req.isWrite && core->cache->sets[setIndex][lineIndex].state == SHARED) {
//                         bus.busRdX(core->id, req.address, cores, s, b);
//                         core->cache->sets[setIndex][lineIndex].state = MODIFIED;
//                     }
//                 }
//                 else {
//                     // Cache miss.
//                     core->cache->misses++;
//                     if (!req.isWrite)
//                         bus.busRd(core->id, req.address, cores, s, b, (1 << b));
//                     else
//                         bus.busRdX(core->id, req.address, cores, s, b);

//                     int victimIndex = core->cache->findReplacementCandidate(setIndex);
//                     CacheLine &victim = core->cache->sets[setIndex][victimIndex];
//                     if (victim.valid && victim.state == MODIFIED) {
//                         core->cache->writeBacks++;
//                         core->cache->idleCycles += 100;  // Write-back penalty.
//                         bus.trafficBytes += (1 << b);
//                     }
//                     if (victim.valid)
//                         core->cache->evictions++;

//                     // Fetch block from memory (100 cycle penalty).
//                     core->cache->idleCycles += 100;
//                     bus.trafficBytes += (1 << b);

//                     victim.valid = true;
//                     victim.tag = tag;
//                     victim.lastUsedCycle = globalCycle + 100;
//                     if (!req.isWrite) {
//                         bool shared = false;
//                         for (Core* other : cores) {
//                             if (other->id == core->id)
//                                 continue;
//                             int idx = other->cache->findLine(setIndex, tag);
//                             if (idx != -1 &&
//                                 other->cache->sets[setIndex][idx].state != INVALID) {
//                                 shared = true;
//                                 break;
//                             }
//                         }
//                         victim.state = shared ? SHARED : EXCLUSIVE;
//                     }
//                     else {
//                         victim.state = MODIFIED;
//                     }
//                     globalCycle += (1 + 100);
//                 }
//             }
//         }
//         // If no core could process an instruction this cycle, advance globalCycle.
//         if (!pending) {
//             uint64_t nextCycle = UINT64_MAX;
//             for (Core* core : cores) {
//                 if (core->instPtr < core->trace.size() &&
//                     core->nextFreeCycle < nextCycle)
//                     nextCycle = core->nextFreeCycle;
//             }
//             if (nextCycle != UINT64_MAX)
//                 globalCycle = nextCycle;
//         }
//     }

//     // Set total cycles for each core.
//     for (Core* core : cores) {
//         core->cache->totalCycles = globalCycle;
//     }
// }

// void Simulator::printResults(const std::string& outFilename) {
//     std::ostream *out;
//     std::ofstream ofs;
//     if (!outFilename.empty()) {
//         ofs.open(outFilename);
//         if (!ofs.is_open()) {
//             std::cerr << "Error opening output file: " << outFilename << std::endl;
//             out = &std::cout;
//         }
//         else {
//             out = &ofs;
//         }
//     }
//     else
//         out = &std::cout;

//     for (Core* core : cores) {
//         *out << "---------- Core " << core->id << " ----------" << std::endl;
//         *out << "Read instructions : " << core->cache->readCount << std::endl;
//         *out << "Write instructions: " << core->cache->writeCount << std::endl;
//         *out << "Total accesses    : " << core->cache->accesses << std::endl;
//         double missRate = (core->cache->accesses > 0) ?
//                           (double)(core->cache->misses) * 100.0 / core->cache->accesses : 0.0;
//         *out << "Cache misses      : " << core->cache->misses << " (" << missRate << "%)" << std::endl;
//         *out << "Evictions         : " << core->cache->evictions << std::endl;
//         *out << "Writebacks        : " << core->cache->writeBacks << std::endl;
//         *out << "Idle cycles       : " << core->cache->idleCycles << std::endl;
//         *out << "Total cycles      : " << core->cache->totalCycles << std::endl;
//         *out << std::endl;
//     }
//     *out << "Global bus invalidations: " << bus.invalidations << std::endl;
//     *out << "Global bus traffic (bytes): " << bus.trafficBytes << std::endl;

//     if (ofs.is_open())
//         ofs.close();
// }
