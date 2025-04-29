#include "Simulator.hh"
#include "Cache.hh"
#include <iostream>
#include <fstream>
#include <climits>
#include <cstdlib>
#include <iomanip>  // Add this for setprecision and fixed#include <iomanip>

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

// (partial implementation - just the run method)
void Simulator::run() {
    uint64_t globalCycle = 0;
    bool pending = false;

    while (true) {
        pending = false;
        if (bus.isbusy && bus.freeCycle == globalCycle && bus.lineIndex != -1) {
            cores[bus.coreid]->cache->busupdate(bus);
        }
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
                // Update the core's instruction pointer and next free cycle in the cache
               core->cache->accessCache(req.isWrite, req.address, globalCycle, core->id, bus, cores); 
            }
        }
        // Review this part
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
    
    // Calculate cache size in KB
    double cacheSizeKB = (1 << s) * E * (1 << b) / 1024.0;
    
    // Print simulation parameters header
    *out << "Simulation Parameters:" << std::endl;
    *out << "Trace Prefix: " << "<trace_prefix>" << std::endl;
    *out << "Set Index Bits: " << s << std::endl;
    *out << "Associativity: " << E << std::endl;
    *out << "Block Bits: " << b << std::endl;
    *out << "Block Size (Bytes): " << (1 << b) << std::endl;
    *out << "Number of Sets: " << (1 << s) << std::endl;
    *out << "Cache Size (KB per core): " << cacheSizeKB << std::endl;
    *out << "MESI Protocol: Enabled" << std::endl;
    *out << "Write Policy: Write-back, Write-allocate" << std::endl;
    *out << "Replacement Policy: LRU" << std::endl;
    *out << "Bus: Central snooping bus" << std::endl;
    *out << std::endl;

    // Print per-core statistics
    for (Core* core : cores) {
        // Calculate total cache misses and miss rate
        int totalMisses = core->cache->readMisses + core->cache->writeMisses;
        int totalAccesses = core->cache->readHits + core->cache->readMisses + 
                            core->cache->writeHits + core->cache->writeMisses;
        double missRate = (totalAccesses > 0) ? 
                          (double)totalMisses * 100.0 / totalAccesses : 0.0;
        int evictions = totalMisses - core->cache->writeBacks;
        
        *out << "Core " << core->id << " Statistics:" << std::endl;
        *out << "Total Instructions: " << core->trace.size() << std::endl;
        *out << "Total Reads: " << core->readCount << std::endl;
        *out << "Total Writes: " << core->writeCount << std::endl;
        *out << "Total Execution Cycles: " << core->execycles << std::endl;
        *out << "Idle Cycles: " << core->cache->idleCycles << std::endl;
        *out << "Cache Misses: " << totalMisses << std::endl;
        *out << "Cache Miss Rate: " << std::fixed << std::setprecision(4) << missRate << "%" << std::endl;
        *out << "Cache Evictions: " << evictions << std::endl;
        *out << "Writebacks: " << core->cache->writeBacks << std::endl;
        *out << "Bus Invalidations: " << bus.invalidations << std::endl;
        *out << "Data Traffic (Bytes): " << bus.trafficBytes << std::endl;
        *out << std::endl;
    }
    
    // Add Overall Bus Summary section
    *out << "Overall Bus Summary:" << std::endl;
    *out << "Total Bus Transactions: " << bus.busTransactions << std::endl;
    *out << "Total Bus Traffic (Bytes): " << bus.trafficBytes << std::endl;

    if (ofs.is_open())
        ofs.close();
}