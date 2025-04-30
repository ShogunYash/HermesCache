# HermesCache

A multi-core cache simulator with MESI coherence protocol.

## Overview

HermesCache is a cache simulator that models a 4-core system with private L1 caches and a central snooping bus for cache coherence. The simulator implements the MESI (Modified-Exclusive-Shared-Invalid) cache coherence protocol to maintain consistency across multiple cores.

## Features

- Simulates a 4-core system with private L1 caches
- Implements MESI cache coherence protocol
- Supports write-back, write-allocate policy
- Uses LRU replacement policy
- Configurable cache parameters (size, associativity, block size)
- Detailed statistics for each core's cache performance
- Optimized memory usage with hashmap-based cache implementation

## Getting Started

### Prerequisites

- C++11 compatible compiler (g++, clang, etc.)
- Make (optional, for using the Makefile)

### Building

Build the project using:

```bash
make
```

This will create an executable named `L1simulate`.

### Running the Simulator

Run the simulator with:

```bash
./L1simulate -t <trace_prefix> -s <set_bits> -E <associativity> -b <block_bits> -o <output_file>
```

Options:
- `-t`: Trace file prefix. The simulator will look for four files named <trace_prefix>_proc0.trace through <trace_prefix>_proc3.trace
- `-s`: Number of set index bits (default: 6)
- `-E`: Associativity/lines per set (default: 2)
- `-b`: Number of block bits/block size (default: 5, meaning 32-byte blocks)
- `-o`: Output file (default: stdout)
- `-h`: Display help message

Example:
```bash
./L1simulate -t app1 -s 6 -E 2 -b 5 -o results
```

### Trace File Format

Each trace file should contain memory access operations, one per line, in the following format:
```
<copilot-edited-file>
```markdown
# HermesCache

A multi-core cache simulator with MESI coherence protocol.

## Overview

HermesCache is a cache simulator that models a 4-core system with private L1 caches and a central snooping bus for cache coherence. The simulator implements the MESI (Modified-Exclusive-Shared-Invalid) cache coherence protocol to maintain consistency across multiple cores.

## Features

- Simulates a 4-core system with private L1 caches
- Implements MESI cache coherence protocol
- Supports write-back, write-allocate policy
- Uses LRU replacement policy
- Configurable cache parameters (size, associativity, block size)
- Detailed statistics for each core's cache performance
- Optimized memory usage with hashmap-based cache implementation

## Getting Started

### Prerequisites

- C++11 compatible compiler (g++, clang, etc.)
- Make (optional, for using the Makefile)

### Building

Build the project using:

```bash
make
```

This will create an executable named `L1simulate`.

### Running the Simulator

Run the simulator with:

```bash
./L1simulate -t <trace_prefix> -s <set_bits> -E <associativity> -b <block_bits> -o <output_file>
```

Options:
- `-t`: Trace file prefix. The simulator will look for four files named <trace_prefix>_proc0.trace through <trace_prefix>_proc3.trace
- `-s`: Number of set index bits (default: 6)
- `-E`: Associativity/lines per set (default: 2)
- `-b`: Number of block bits/block size (default: 5, meaning 32-byte blocks)
- `-o`: Output file (default: stdout)
- `-h`: Display help message

Example:
```bash
./L1simulate -t app1 -s 6 -E 2 -b 5 -o results
```

### Trace File Format

Each trace file should contain memory access operations, one per line, in the following format:
