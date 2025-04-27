#include <iostream>
#include <cstdlib>
#include <string>
#include "Simulator.hh"

void printHelp(char* programName) {
    std::cout << "Usage: " << programName
              << " -t <tracefileBase> -s <s> -E <E> -b <b> -o <outfilename>\n";
}

int main(int argc, char* argv[]) {
    // Default parameters (e.g., for a 4KB 2-way set associative cache with 32-byte blocks)
    int s = 6;                // s = 6 → 2^6 = 64 sets (4KB cache: 64 sets * 2 lines * 32 bytes)
    int E = 2;                // 2-way associativity
    int b = 5;                // 2^5 = 32-byte block size
    std::string traceBaseName = "app1"; // e.g., app1_proc0.trace, etc.
    std::string outFilename = "";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-t" && i + 1 < argc) {
            traceBaseName = argv[++i];
        } else if (arg == "-s" && i + 1 < argc) {
            s = std::stoi(argv[++i]);
        } else if (arg == "-E" && i + 1 < argc) {
            E = std::stoi(argv[++i]);
        } else if (arg == "-b" && i + 1 < argc) {
            b = std::stoi(argv[++i]);
        } else if (arg == "-o" && i + 1 < argc) {
            outFilename = argv[++i];
        } else if (arg == "-h") {
            printHelp(argv[0]);
            exit(EXIT_SUCCESS);
        }
    }

    Simulator sim(s, E, b);
    sim.loadTraces(traceBaseName);
    sim.run();
    sim.printResults(outFilename);

    return 0;
}
