# BGP Simulator - Technical Documentation

## Project Overview

BGP simulator implementing route propagation across AS-level Internet topology. Built for CSE3150 course project requirements (see `CSE3150_project_specification.md` for full specification). Processes CAIDA AS relationship data and simulates BGP announcements with valley-free routing enforcement and ROV (Route Origin Validation) support.

## Requirements Analysis

### Core Requirements (from CSE3150 specification)
1. **AS Graph Construction**: Parse CAIDA AS relationship data (P2C, C2P, P2P relationships)
2. **Cycle Detection**: Identify and handle cycles in provider-customer relationships
3. **Route Propagation**: Implement BGP announcement propagation following valley-free routing principles
4. **Path Selection**: Apply BGP path preference rules for route selection
5. **ROV Support**: Implement Route Origin Validation
6. **Performance**: Execute within ≤2CPU/8GB resource constraints
7. **I/O**: Process CSV input/output formats for announcements and results

### Implementation Approach

Direct C++20 implementation of specification requirements. No Python bindings in current version. Single-threaded by default with optional multi-threaded queue processing (up to 16 threads via CLI argument).

## Architecture and Implementation

### AS Graph (`as_graph.h/cpp`)

**Data Structure**:
- `vector<ASNode> nodes` storing all ASes in a compact index-based layout
- `unordered_map<ASN, uint32_t> asn_to_index` mapping ASN → node index
- `ASNode` contains:
  - `ASN asn` (node ID)
  - `vector<uint32_t> providers, customers, peers` (neighbor indices)
  - `BGPState* state` (pointer to per-AS BGP state, assigned by simulator)
  - `int rank` (for propagation order)

**CAIDA Parsing**:
- Stream-based bz2 decompression via `BZ2_bzRead`
- Line-by-line parsing with leftover buffer for chunk boundaries
- Format: `ASN1|ASN2|type` where type=-1 (P2C), 0 (P2P)
- Bidirectional edge insertion (provider↔customer, peer↔peer)

**Cycle Detection**:
- DFS traversal of provider edges only
- Ignores peer relationships (cycles allowed in peer mesh)
- Returns true if back-edge detected in provider tree
- O(V+E) complexity

**Rank Flattening**:
- BFS from nodes with no customers (rank 0)
- Assigns increasing rank to providers at each level
- Returns `vector<vector<uint32_t>>` indexed by rank (node indices)
- Simulator uses indices to traverse ranks; ASNs are read from `ASNode::asn`

### Route Announcements (`announcement.h`)
- **Structure**: POD (Plain Old Data) struct with `PrefixID` (integer prefix identifier), ASN vector path, next_hop ASN, relationship type enum, and ROV validation flag
- **Path Selection**: Implemented via overloaded `operator>` comparing relationship priority > path length > next_hop value

**Relationship Priority Hierarchy**:
1. ORIGIN (3) - Origin AS
2. CUST (2) - Customer
3. PEER (1) - Peer
4. PROV (0) - Provider

### BGP Engine (`bgp.h/cpp`)

**Per-AS State (`BGPState`)**:
- `unordered_map<PrefixID, Announcement> rib` - one entry per prefix ID
- `unordered_map<PrefixID, Announcement> recv_queue` - best buffered announcement per prefix ID (pending)
- `bool is_rov` - if true, drop announcements with `rov_invalid == true` on receive

**API**:
- `bgp_receive(BGPState&, PrefixID, const Announcement&)` updates the per-prefix candidate using `operator>` (with optional ROV filtering)
- `bgp_process_queue(BGPState&, ASN self_asn)` finalizes the best candidate per prefix and updates the local RIB

**Conflict Resolution** (in `bgp_process_queue`):
1. Per prefix ID, take the single best candidate from `recv_queue`
2. Prepend self ASN to AS-path if not already present
3. Store best announcement in `rib`
4. Clear `recv_queue`

**Announcement Comparison** (`operator>` in `announcement.h`):
1. Relationship: ORIGIN > CUST > PEER > PROV
2. If equal: shorter AS-path wins
3. If equal: lower next_hop ASN wins

## Simulator Logic (`simulator.cpp`)

### Input Processing

**Announcements CSV** (`anns.csv`):
```
asn,prefix,rov_invalid
1,1.2.0.0/16,False
666,1.2.0.0/16,True
```
- Parsed line-by-line after skipping header using manual comma splitting and `std::from_chars`
- Creates `Announcement` with origin ASN, ORIGIN relationship, `rov_invalid` flag and `PrefixID`
- Directly inserted into origin AS's RIB as initial seed

**ROV ASNs** (`rov_asns.csv`):
```
4
3
```
- Line-separated ASNs deploying ROV
- Stored in `unordered_set<ASN>` for O(1) lookup using `std::from_chars`
- Used to set `BGPState::is_rov` during initialization

### Propagation Algorithm

**Phase 1 - UP** (customers → providers):
```cpp
for (rank = 0; rank < max_rank; ++rank) {
    for each AS in rank:
        process_queue(asn)  // Resolve conflicts
        send to all providers with rel=PROV
}
```

**Phase 2 - PEERS** (lateral, one hop):
```cpp
for each AS:
    send to all peers with rel=PEER
for each AS:
    process_queue(asn)
```
Critical: All sends before any process to prevent multi-hop peer propagation.

**Phase 3 - DOWN** (providers → customers):
```cpp
for (rank = max_rank; rank >= 0; --rank) {
    for each AS in rank:
        send to all customers with rel=CUST
    for each AS in rank:
        process_queue(asn)
}
```

### Output Format

CSV with columns: `asn,prefix,as_path`
```
1,1.2.0.0/16,3-2-1
4,1.2.0.0/16,4-2-1
```
AS-path is dash-separated, written left-to-right (closest to current AS first).

## Performance Characteristics

### Algorithmic Complexity
- **Graph Construction**: O(E) where E = number of relationships
- **Cycle Detection**: O(V+E) DFS traversal
- **Rank Calculation**: O(V+E) BFS
- **Propagation**: O(V×P×A) per phase (A = average number of announcements per prefix/AS; best candidate per prefix is maintained incrementally in `bgp_receive`)
- **Output**: O(V×P) to write all RIBs

### Memory and Layout Optimizations
- Index-based AS graph (`vector<ASNode>` + `asn_to_index`) for better cache locality
- Relationship vectors store neighbor indices instead of ASNs
- `reserve()` for `as_path` (16 typical depth), relationship vectors (8 typical degree)
- Per-AS `BGPState` maps (`rib` and `recv_queue`) reserve initial capacity to reduce rehashing
- Move semantics for announcements where possible
- Single `BGPState` per AS (no copies)
- Stream-based bz2 parsing with 8KB buffer, leftover handling and lightweight `std::from_chars` parsing

### Benchmark
- **Mini regression test** (repository):
  - Inputs: `scripts/tests/mini_anns.csv`, `scripts/tests/mini_rov.csv`
  - Expected output: `scripts/tests/mini_expected.csv`
  - Runner: `scripts/run_tests.sh` builds in Release mode, runs `bgp_sim` and compares sorted CSV rows

- **Timings (mini dataset)** using `scripts/benchmarks/run_benchmarks.sh`:
  - `threads = 1`: ~0.22 s
  - `threads = 2, 4, 8, 16`: ~0.002 s (dominated by startup/overhead)

- **Timings (test_anns/test_rov dataset)** using `scripts/benchmarks/run_benchmarks.sh large`:
  - `threads = 1`: ~0.28 s
  - `threads = 2`: ~0.003 s
  - `threads = 4`: ~0.002 s
  - `threads = 8`: ~0.003 s
  - `threads = 16`: ~0.003 s

These datasets are synthetic and small, primarily intended to verify functionality and the benchmark harness. Real CAIDA-scale inputs will produce higher absolute runtimes and more meaningful multi-thread scaling, depending on hardware.

### Compiler Flags
```cmake
-O3 -march=native -mtune=native -ffast-math -funroll-loops -DNDEBUG -flto
```
- `-march=native`: Use CPU-specific instructions
- `-flto`: Link-time optimization
- `-ffast-math`: Floating point not used, but enables some optimizations
- No hardcoded architecture flags beyond `-march=native` (portable across x86_64/ARM machines)

### Usage
```bash
./bgp_sim <announcements.csv> <rov_asns.csv> [threads] > output_ribs.csv
```

## What is NOT Implemented

To maintain accuracy, the following are explicitly **not** in this implementation:

### Not Implemented (out of scope):
- **Python bindings** (spec mentions optional, not implemented)
- **libcurl integration** (CAIDA file assumed local, no downloading)
- **Advanced BGP features**: Communities, MED, LOCAL_PREF, AS-PATH prepending beyond natural propagation
- **Prefix validation**: IPv4/IPv6 format checking (treated as opaque strings)
- **AS-PATH loop prevention** (beyond cycle detection in topology)
- **Custom memory pools** (`mempool.h`/`ringbuf.h` exist but unused in current version)
- **Incremental updates** (static simulation only, no route withdrawals)
- **Formal performance report** (simple local benchmark script exists; large CAIDA-scale performance depends on dataset and hardware)

### Hardcoded Assumptions:
- CAIDA file path: `data/as-rel.txt.bz2`
- CSV format fixed (no header configuration)
- Cycle detection exits immediately (no recovery)
 - Large announcement datasets are not shipped with this repository and are expected to be provided externally when needed

## Design Rationale

**Why Flat BGPState?**
- Eliminates virtual calls on the hot path
- Keeps all per-AS BGP data in a compact struct (RIB, receive queue, ROV flag)
- ROV behavior is a simple flag check in `bgp_receive`

**Why Three-Phase Propagation?**
- Matches valley-free routing semantics
- UP: announcements flow toward Tier-1 providers
- PEERS: lateral exchange without creating valleys
- DOWN: distribution to customers

**Why Rank-Based Iteration?**
- Guarantees correct propagation order
- Prevents announcements arriving before their dependencies
- Enables easy verification (leaf nodes process first)

## Known Limitations

1. **No prefix aggregation** - each prefix handled independently
2. **String-based prefix storage** - no IP parsing or subnet math
3. **Single CAIDA file** - no support for multiple sources or updates
4. **Fixed CSV format** - no flexible parsing
5. **Memory grows with RIB size** - no RIB size limits or pruning
6. **No logging or debugging output** (beyond load/cycle messages)

## Conclusion

This implementation fulfills CSE3150 specification requirements:
- ✓ AS graph construction from CAIDA
- ✓ Cycle detection (P2C/C2P only)
- ✓ Valley-free routing via three-phase propagation
- ✓ BGP path selection rules (relationship > length > next-hop)
- ✓ ROV support
- ✓ CSV input/output
- ✓ <2CPU, <8GB constraints (single-threaded by default, stream-based parsing)

Design prioritizes correctness and clarity, while applying targeted optimizations (index-based graph, PrefixID keys, flat `BGPState`, optional queue parallelism) to comfortably meet performance requirements.