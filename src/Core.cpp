#include "Core.hh"
#include <fstream>
#include <iostream>
#include <cstdlib>

Request::Request(bool isWrite, uint32_t address) : isWrite(isWrite), address(address) {}

Core::Core(int id, Cache* cache) : id(id), cache(cache), instPtr(0), nextFreeCycle(0), readCount(0), writeCount(0) {}

void Core::loadTrace(const std::string& filename) {
    std::ifstream fin(filename);
    if (!fin.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string line;
    while (getline(fin, line)) {
        if (line.empty())
            continue;
        std::istringstream iss(line);
        char op;
        std::string addrStr;
        iss >> op >> addrStr;
        // Remove "0x" prefix if present.
        if (addrStr.substr(0, 2) == "0x" || addrStr.substr(0, 2) == "0X")
            addrStr = addrStr.substr(2);
        uint32_t address = stoul(addrStr, nullptr, 16);
        bool isWrite = (op == 'W' || op == 'w');
        trace.emplace_back(isWrite, address);
        
        // Update the read/write counts while loading the trace
        if (isWrite) {
            writeCount++;
        } else {
            readCount++;
        }
    }
    fin.close();
}
