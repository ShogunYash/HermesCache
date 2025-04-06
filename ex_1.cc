#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <getopt.h>

using namespace std;

// MESI states for cache lines
enum MESI {
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};

enum RequestType {
    READ,
    WRITE
};

// Structure for each cache line
struct CacheLine {
    bool valid;
    uint32_t tag;
    int lastUsedCycle;  // For LRU replacement
    MESI state;
    CacheLine() : valid(false), tag(0), lastUsedCycle(0), state(INVALID) { }
};

// Class representing an L1 data cache
class Cache {
public:
    int s;               // Number of set index bits: total sets = 2^s
    int E;               // Associativity: number of cache lines per set
    int b;               // Number of block bits: block size in bytes = 2^b
    int numSets;         // Calculated as 1 << s
    int blockSize;       // = 1 << b

    // Each cache is a vector of sets. Each set is a vector of cache lines.
    vector<vector<CacheLine>> sets;

    // Statistics for the core using this cache
    uint64_t accesses;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writeBacks;
    uint64_t readCount;
    uint64_t writeCount;
    uint64_t idleCycles;
    uint64_t totalCycles;

    Cache(int s, int E, int b)
        : s(s), E(E), b(b), accesses(0), misses(0), evictions(0),
          writeBacks(0), readCount(0), writeCount(0), idleCycles(0), totalCycles(0)
    {
        numSets = 1 << s;
        blockSize = 1 << b;
        sets.resize(numSets, vector<CacheLine>(E));
    }

    // Split the address into set index and tag
    void getAddressParts(uint32_t addr, uint32_t& setIndex, uint32_t& tag) {
        setIndex = (addr >> b) & ((1 << s) - 1);
        tag = addr >> (s + b);
    }

    // Find a cache line matching the tag in the set.
    int findLine(int setIndex, uint32_t tag) {
        for (int i = 0; i < E; i++) {
            if (sets[setIndex][i].valid && sets[setIndex][i].tag == tag)
                return i;
        }
        return -1;
    }

    // Find a candidate line for replacement (use LRU)
    int findReplacementCandidate(int setIndex) {
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
};

// Memory request structure (read or write)
struct Request {
    RequestType type;
    uint32_t address;
};

// Each core holds a pointer to its cache and a trace of requests
struct Core {
    int id;
    Cache* cache;
    vector<Request> trace;
    size_t instPtr;      // Instruction pointer in the trace
    uint64_t nextFreeCycle;   // The core is busy until this cycle

    Core(int id, Cache* cache) : id(id), cache(cache), instPtr(0), nextFreeCycle(0) { }
};

// Main Simulator class that handles four cores, bus operations and global cycle count
class Simulator {
public:
    int s, E, b;
    vector<Core*> cores;
    uint64_t globalCycle;
    uint64_t busTrafficBytes;
    uint64_t invalidations;

    Simulator(int s, int E, int b) : s(s), E(E), b(b), globalCycle(0),
                                     busTrafficBytes(0), invalidations(0) { }

    // Load trace files with the expected naming convention: baseName_procX.trace
    void loadTraces(const string& baseName) {
        for (int i = 0; i < 4; i++) {
            string filename = baseName + "_proc" + to_string(i) + ".trace";
            ifstream fin(filename);
            if (!fin.is_open()) {
                cerr << "Error opening file: " << filename << endl;
                exit(EXIT_FAILURE);
            }
            Cache* cache = new Cache(s, E, b);
            Core* core = new Core(i, cache);
            string line;
            while (getline(fin, line)) {
                if (line.empty())
                    continue;
                istringstream iss(line);
                char op;
                string addrStr;
                iss >> op >> addrStr;
                // Remove any potential leading "0x" and convert hex to integer
                uint32_t addr = stoul(addrStr, nullptr, 16);
                Request req;
                req.address = addr;
                req.type = (op == 'R' ? READ : WRITE);
                core->trace.push_back(req);
            }
            fin.close();
            cores.push_back(core);
        }
    }

    // Bus Read: Simulate a BusRd transaction (for read misses)
    // Other cores check if they have the block and if in MODIFIED or EXCLUSIVE state.
    void busRd(int requesterId, uint32_t address) {
        uint32_t setIndex = (address >> b) & ((1 << s) - 1);
        uint32_t tag = address >> (s + b);
        for (Core* other : cores) {
            if (other->id == requesterId)
                continue;
            int lineIndex = other->cache->findLine(setIndex, tag);
            if (lineIndex != -1) {
                CacheLine &line = other->cache->sets[setIndex][lineIndex];
                if (line.state == MODIFIED) {
                    // Write back current block to memory; incur penalty and add to bus traffic.
                    other->cache->idleCycles += 100;
                    busTrafficBytes += other->cache->blockSize;
                    // Transition to SHARED state after write-back.
                    line.state = SHARED;
                } else if (line.state == EXCLUSIVE) {
                    line.state = SHARED;
                }
                // For SHARED, state remains unchanged.
            }
        }
    }

    // Bus Read Exclusive: Simulate a BusRdX transaction (for write misses)
    void busRdX(int requesterId, uint32_t address) {
        uint32_t setIndex = (address >> b) & ((1 << s) - 1);
        uint32_t tag = address >> (s + b);
        for (Core* other : cores) {
            if (other->id == requesterId)
                continue;
            int lineIndex = other->cache->findLine(setIndex, tag);
            if (lineIndex != -1) {
                CacheLine &line = other->cache->sets[setIndex][lineIndex];
                if (line.valid && line.state != INVALID) {
                    // Invalidate the line in other caches.
                    invalidations++;
                    line.state = INVALID;
                }
            }
        }
    }

    // Run simulation until all cores have processed their trace files.
    void run() {
        bool pending = true;
        // Run cycle by cycle.
        while (pending) {
            pending = false;
            for (Core* core : cores) {
                // If this core has pending instructions and is not blocked
                if (core->instPtr < core->trace.size() && core->nextFreeCycle <= globalCycle) {
                    pending = true;
                    Request req = core->trace[core->instPtr];
                    core->instPtr++;
                    core->cache->accesses++;
                    if (req.type == READ)
                        core->cache->readCount++;
                    else
                        core->cache->writeCount++;

                    uint32_t setIndex, tag;
                    core->cache->getAddressParts(req.address, setIndex, tag);
                    int lineIndex = core->cache->findLine(setIndex, tag);
                    bool hit = (lineIndex != -1 &&
                                core->cache->sets[setIndex][lineIndex].state != INVALID);

                    if (hit) {
                        // Cache hit costs 1 cycle.
                        globalCycle += 1;
                        core->cache->sets[setIndex][lineIndex].lastUsedCycle = globalCycle;
                        // For writes, if the state is SHARED then trigger a BusRdX.
                        if (req.type == WRITE) {
                            if (core->cache->sets[setIndex][lineIndex].state == SHARED) {
                                busRdX(core->id, req.address);
                                core->cache->sets[setIndex][lineIndex].state = MODIFIED;
                            } else if (core->cache->sets[setIndex][lineIndex].state == EXCLUSIVE) {
                                core->cache->sets[setIndex][lineIndex].state = MODIFIED;
                            }
                        }
                    } else {
                        // Cache miss: update miss count and incur additional delay.
                        core->cache->misses++;
                        if (req.type == READ)
                            busRd(core->id, req.address);
                        else
                            busRdX(core->id, req.address);
                        
                        // Find a replacement candidate in the set.
                        int replaceIndex = core->cache->findReplacementCandidate(setIndex);
                        CacheLine &line = core->cache->sets[setIndex][replaceIndex];
                        // If replacing a valid cache line in MODIFIED state, writeback is needed.
                        if (line.valid && line.state == MODIFIED) {
                            core->cache->writeBacks++;
                            core->cache->idleCycles += 100;
                            busTrafficBytes += core->cache->blockSize;
                        }
                        if (line.valid)
                            core->cache->evictions++;
                        
                        // Bring the block from memory (simulate delay of 100 cycles).
                        core->cache->idleCycles += 100;
                        busTrafficBytes += core->cache->blockSize;
                        // Update the cache line.
                        line.valid = true;
                        line.tag = tag;
                        line.lastUsedCycle = globalCycle + 100;
                        if (req.type == READ) {
                            // If any other cache has the line, set state to SHARED; otherwise, EXCLUSIVE.
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
                            line.state = shared ? SHARED : EXCLUSIVE;
                        } else {
                            // For writes, the cache line is brought in and set to MODIFIED.
                            line.state = MODIFIED;
                        }
                        globalCycle += 1 + 100; // additional delay due to miss and block fill.
                    }
                }
            }
            // Advance global cycle if no core is ready but some instructions remain.
            if (!pending) {
                uint64_t nextCycle = UINT64_MAX;
                for (Core* core : cores) {
                    if (core->instPtr < core->trace.size() && core->nextFreeCycle < nextCycle)
                        nextCycle = core->nextFreeCycle;
                }
                if (nextCycle != UINT64_MAX)
                    globalCycle = nextCycle;
            }
        }
        // Mark the final total cycles for each core.
        for (Core* core : cores) {
            core->cache->totalCycles = globalCycle;
        }
    }

    // Report simulation results.
    void printResults() {
        for (Core* core : cores) {
            cout << "Core " << core->id << " Results:" << endl;
            cout << "  Read instructions : " << core->cache->readCount << endl;
            cout << "  Write instructions: " << core->cache->writeCount << endl;
            cout << "  Total accesses    : " << core->cache->accesses << endl;
            cout << "  Cache misses      : " << core->cache->misses 
                 << " (Miss rate: " << (double)core->cache->misses * 100 / core->cache->accesses << "%)" << endl;
            cout << "  Evictions         : " << core->cache->evictions << endl;
            cout << "  Writebacks        : " << core->cache->writeBacks << endl;
            cout << "  Idle cycles       : " << core->cache->idleCycles << endl;
            cout << "  Total cycles      : " << core->cache->totalCycles << endl;
            cout << endl;
        }
        cout << "Global bus traffic (in bytes): " << busTrafficBytes << endl;
        cout << "Total invalidations on bus   : " << invalidations << endl;
    }
};

int main(int argc, char* argv[]) {
    // Default parameters: 4KB cache (s=12 leads to 4096 sets if each entry is small),
    // 2-way set associative, block size = 2^b bytes.
    int s = 12;  
    int E = 2;
    int b = 5;  // For a 32-byte block: 2^5 = 32.
    string traceBaseName = "app1";  // Expects files like app1_proc0.trace, etc.
    string outFileName = "";

    int opt;
    while ((opt = getopt(argc, argv, "t:s:E:b:o:h")) != -1) {
        switch (opt) {
            case 't':
                traceBaseName = optarg;
                break;
            case 's':
                s = stoi(optarg);
                break;
            case 'E':
                E = stoi(optarg);
                break;
            case 'b':
                b = stoi(optarg);
                break;
            case 'o':
                outFileName = optarg;
                break;
            case 'h':
                cout << "Usage: " << argv[0]
                     << " -t <tracefileBase> -s <s> -E <E> -b <b> -o <outfile>" << endl;
                exit(EXIT_SUCCESS);
            default:
                break;
        }
    }

    Simulator sim(s, E, b);
    sim.loadTraces(traceBaseName);
    sim.run();
    sim.printResults();

    return 0;
}
