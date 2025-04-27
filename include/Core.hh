#ifndef CORE_H
#define CORE_H

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include "Cache.hh"

// A Request represents a memory access operation.
struct Request {
    bool isWrite;       // true for write; false for read
    uint32_t address;   // 32-bit memory address

    Request(bool isWrite, uint32_t address);
};

// Core represents a processor core with its own cache and execution trace.
class Core {
public:
    int id;                     // Core identifier (0 to 3)
    Cache* cache;               // Pointer to the core's L1 cache
    std::vector<Request> trace; // Trace of memory requests for the core
    size_t instPtr;             // Instruction pointer in the trace
    uint64_t nextFreeCycle;     // Cycle count when the core becomes unblocked
    uint64_t readCount;         // Total read operations
    uint64_t writeCount;        // Total write operations

    Core(int id, Cache* cache);
    // Loads a trace file into the core's trace vector.
    void loadTrace(const std::string& filename);
};

#endif // CORE_H
