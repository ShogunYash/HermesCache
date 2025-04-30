#include "Core.hh"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <string>
#include <algorithm>

Request::Request(bool isWrite, uint32_t address) : isWrite(isWrite), address(address) {}

Core::Core(int id, Cache* cache) : id(id), cache(cache), instPtr(0), previnstr(0), nextFreeCycle(0), readCount(0), writeCount(0), execycles(0) {}

void Core::loadTrace(const std::string& filename) {
    std::ifstream fin(filename);
    if (!fin.is_open()) {
        std::cerr << "Warning: Could not open trace file: " << filename << std::endl;
        return;
    }
    
    std::string line;
    while (getline(fin, line)) {
        // Skip empty lines or comment lines
        if (line.empty() || line[0] == '#')
            continue;
            
        // Extract operation character (R or W)
        if (line.length() < 1) continue;
        char op = line[0];
        
        // Validate operation type
        if (op != 'R' && op != 'r' && op != 'W' && op != 'w') {
            std::cerr << "Warning: Invalid operation in file " << filename << ": " << op << std::endl;
            continue;
        }
        
        // Find address part (everything after the operation character)
        std::string addressPart = line.substr(1);
        
        // Trim whitespace from address part
        addressPart.erase(0, addressPart.find_first_not_of(" \t\r\n"));
        addressPart.erase(addressPart.find_last_not_of(" \t\r\n") + 1);
        
        // Remove '0x' prefix if present
        if (addressPart.length() >= 2 && addressPart.substr(0, 2) == "0x") {
            addressPart = addressPart.substr(2);
        }
        
        // Convert hex string to integer
        uint32_t address;
        try {
            address = std::stoul(addressPart, nullptr, 16);
        } catch (const std::exception& e) {
            std::cerr << "Error in file " << filename << ": Failed to convert '" 
                      << addressPart << "' to address. Line: '" << line << "'" << std::endl;
            continue;
        }
        
        // Add instruction to trace
        bool isWrite = (op == 'W' || op == 'w');
        trace.emplace_back(isWrite, address);
        
        // Update read/write counters
        if (isWrite) {
            writeCount++;
        } else {
            readCount++;
        }
    }
    
    fin.close();
    
    if (trace.empty()) {
        std::cerr << "Warning: No valid operations loaded from trace file: " << filename << std::endl;
    }
}
