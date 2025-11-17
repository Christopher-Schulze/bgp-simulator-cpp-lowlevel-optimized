# BGP Simulator

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

BGP route propagation simulator for AS-level Internet topology analysis. Implements valley-free routing with ROV support. Built for CSE3150 course project.

## Features

- **AS Topology**: Parses CAIDA AS relationship data (provider-customer, peer-peer)
- **Cycle Detection**: DFS-based validation of provider-customer hierarchy
- **Valley-Free Routing**: Three-phase propagation (up/peers/down) prevents routing valleys
- **BGP Path Selection**: Relationship priority > path length > next-hop ASN
- **ROV**: Route Origin Validation drops invalid announcements
- **BGP Engine**: Flat per-AS state (`BGPState`) with PrefixID-based RIB and receive queues, optional ROV flag
- **Low-level Parsing**: Manual bz2/CSV parsing with `std::string_view` and `std::from_chars` (no `stringstream` on hot paths)
- **Optimized Layout**: Index-based AS graph (`vector<ASNode>` + `asn_to_index`), integer PrefixIDs, targeted `reserve()` and move semantics
- **Optional Parallelism**: Multi-threaded queue processing (up to 16 threads via CLI), deterministic output equivalent to single-threaded runs
- **C++20**: Modern C++ with STL containers, no external dependencies except BZip2

## Design Highlights

- **Index-based AS graph**: `vector<ASNode>` + `asn_to_index` for cache-friendly node access and O(1) ASN lookup.
- **Flat per-AS BGP state**: `BGPState` struct with PrefixID-based RIB and receive queue (no virtual dispatch on the hot path).
- **Manual parsing**: CAIDA bz2 and CSV files parsed with `std::string_view` + `std::from_chars` instead of `std::istringstream`.
- **Conflict resolution**: Single best candidate per prefix maintained incrementally, minimizing per-phase work.
- **Optional parallelism**: Queue processing can run on multiple threads with deterministic output identical to single-threaded runs.
- **Tight testing loop**: Mini regression test and benchmark scripts for quick verification and timing.

## Build

### Requirements
- **Compiler**: C++20 (GCC 10+, Clang 10+, MSVC 19.20+)
- **CMake**: 3.20+
- **BZip2**: Development headers (`libbz2-dev` on Debian/Ubuntu, `bzip2-devel` on RHEL/Fedora)

### Build Steps

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)  # or make -j$(sysctl -n hw.ncpu) on macOS
```

Output: `build/bgp_sim`

## Usage

```bash
./bgp_sim <announcements.csv> <rov_asns.csv> [threads] > output.csv
```

- If `threads` is omitted, the simulator runs single-threaded.
- If `threads` is provided, it is clamped to `min(threads, 16, hardware_concurrency)`.

### Input Format

**announcements.csv**:
```csv
asn,prefix,rov_invalid
1,1.2.0.0/16,False
666,1.2.0.0/16,True
```

**rov_asns.csv** (line-separated):
```
3
4
```

### Output Format

**output.csv**:
```csv
asn,prefix,as_path
1,1.2.0.0/16,1
2,1.2.0.0/16,2-1
3,1.2.0.0/16,3-2-1
```

AS-path is dash-separated, left-to-right (closest AS first).

## Implementation

### Algorithm

1. **Load AS Graph**: Parse `data/as-rel.txt.bz2` (CAIDA format: `ASN1|ASN2|type`)
2. **Cycle Check**: DFS on provider edges, exit if cycle detected
3. **Rank Assignment**: BFS from leaf nodes upward
4. **Seed Announcements**: Parse input CSV, insert into origin AS RIBs
5. **Propagate UP**: Rank 0→N, send to providers
6. **Propagate PEERS**: All-to-all peer exchange (one hop)
7. **Propagate DOWN**: Rank N→0, send to customers
8. **Output**: Write all AS RIBs to CSV

### Path Selection Rules

1. **Relationship**: Origin > Customer > Peer > Provider
2. **Path Length**: Shorter wins
3. **Next-Hop ASN**: Lower wins (tiebreaker)

### ROV Behavior

ASes in `rov_asns.csv` drop announcements with `rov_invalid=True` at receive time.

## Tests & Benchmarks

- **scripts/run_tests.sh**: Mini regression tests (single- and multi-thread)
- **scripts/benchmarks/run_benchmarks.sh**: Simple timing harness (1/2/4/8/16 threads)

## Project Structure

```
├── src/
│   ├── simulator.cpp      # Main entry point (CSV I/O, propagation logic, optional multithreading)
│   ├── as_graph.h/cpp     # AS graph (CAIDA parsing, cycle detection, ranks; index-based layout)
│   ├── bgp.h/cpp          # BGP engine (BGPState and helper functions)
│   ├── announcement.h     # Announcement struct with comparison operator
│   ├── main.cpp           # Benchmark helper (no main, for testing only)
│   ├── mempool.h          # Memory pool (unused in current version)
│   ├── ringbuf.h          # Ring buffer (unused in current version)
│   └── allocator.h        # Allocator (unused in current version)
├── data/
│   └── as-rel.txt.bz2     # CAIDA AS relationship data (must be provided)
├── docs/
│   ├── documentation.md   # Technical documentation
│   └── implementation_notes.txt  # Design notes
├── scripts/
│   ├── build.sh           # Build helper
│   ├── run_tests.sh       # Mini regression tests (single- and multi-thread)
│   ├── run_benchmarks.sh  # Simple timing harness (1/2/4/8/16 threads)
│   └── tests/             # Example CSV test data (mini_*.csv, test_*.csv)
├── CSE3150_project_specification.md  # CSE3150 course project specification
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## Documentation

- **[docs/documentation.md](docs/documentation.md)**: Detailed technical documentation
- **[CSE3150_project_specification.md](CSE3150_project_specification.md)**: Original CSE3150 project specification
- **[docs/implementation_notes.txt](docs/implementation_notes.txt)**: Design decisions and rationale

## License

MIT License - see [LICENSE](LICENSE) for details.

Copyright (c) 2025 Christopher Schulze