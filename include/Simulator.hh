#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <string>
#include <vector>
#include "Core.hh"
#include "Bus.hh"

// Simulator coordinates all cores, caches, and bus transactions.
class Simulator {
private:
    int s, E, b;                // Cache configuration parameters
    std::vector<Core*> cores;   // Four processor cores
    Bus bus;                    // The bus for cache coherence transactions
    uint64_t globalCycle;       // Global simulation cycle
    uint64_t totalCycles;  // Add this field to store the total simulation cycles

public:
    Simulator(int s, int E, int b);
    ~Simulator();
    // Loads the trace files (expects baseName_proc0.trace ... baseName_proc3.trace).
    void loadTraces(const std::string& baseName);
    // Runs the simulation until all cores have completed their traces.
    void run();
    // Prints simulation results; if outFilename is nonempty, writes to that file.
    void printResults(const std::string& outFilename = "");
};

#endif // SIMULATOR_H
