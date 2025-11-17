#include "as_graph.h"
#include <iostream>
#include <chrono>

// Benchmark helper: loads the AS graph and runs basic checks
void benchmark_graph() {
    auto start = std::chrono::high_resolution_clock::now();
    ASGraph graph;
    if (!graph.load_from_caida("data/as-rel.txt.bz2")) {
        std::cerr << "Error loading CAIDA data" << std::endl;
        return;
    }
    std::cout << "Nodes: " << graph.nodes.size() << std::endl;
    if (graph.detect_cycles()) {
        std::cerr << "Cycles detected in provider/customer relationships" << std::endl;
        return;
    }
    auto ranks = graph.flatten_ranks();
    std::cout << "Max rank: " << ranks.size() - 1 << std::endl;
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;
}