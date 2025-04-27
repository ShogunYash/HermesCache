#include <iostream>
#include <cstdlib>
#include <getopt.h>
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

    int opt;
    while ((opt = getopt(argc, argv, "t:s:E:b:o:h")) != -1) {
        switch(opt) {
            case 't':
                traceBaseName = std::string(optarg);
                break;
            case 's':
                s = std::stoi(optarg);
                break;
            case 'E':
                E = std::stoi(optarg);
                break;
            case 'b':
                b = std::stoi(optarg);
                break;
            case 'o':
                outFilename = std::string(optarg);
                break;
            case 'h':
                printHelp(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                break;
        }
    }

    Simulator sim(s, E, b);
    sim.loadTraces(traceBaseName);
    sim.run();
    sim.printResults(outFilename);

    return 0;
}
