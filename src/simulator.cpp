#include "as_graph.h"
#include "announcement.h"
#include "bgp.h"
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <string_view>
#include <charconv>
#include <thread>

// Global prefix dictionary for this simulation run
static std::unordered_map<std::string, PrefixID> g_prefix_to_id;
static std::vector<std::string> g_id_to_prefix;

static PrefixID get_prefix_id(const std::string& prefix) {
    auto it = g_prefix_to_id.find(prefix);
    if (it != g_prefix_to_id.end()) return it->second;
    PrefixID id = static_cast<PrefixID>(g_id_to_prefix.size());
    g_id_to_prefix.push_back(prefix);
    g_prefix_to_id.emplace(prefix, id);
    return id;
}

static void send_announcements(ASGraph& graph, uint32_t from_idx, const std::vector<uint32_t>& targets, Rel rel_type) {
    if (from_idx >= graph.nodes.size()) return;
    ASNode& from_node = graph.nodes[from_idx];
    if (!from_node.state) return;
    ASN from_asn = from_node.asn;
    for (const auto& [prefix_id, ann] : from_node.state->rib) {
        for (uint32_t target_idx : targets) {
            if (target_idx >= graph.nodes.size()) continue;
            ASNode& target_node = graph.nodes[target_idx];
            if (!target_node.state) continue;
            Announcement new_ann = ann;
            new_ann.next_hop = from_asn;
            new_ann.rel = rel_type;
            bgp_receive(*target_node.state, prefix_id, new_ann);
        }
    }
}

// Helper: parallel for over a vector of node indices. For num_threads <= 1,
// executes sequentially to avoid thread overhead.
template <typename Func>
static void parallel_for_indices(const std::vector<uint32_t>& indices,
                                 unsigned num_threads,
                                 Func&& fn) {
    if (indices.empty()) return;
    if (num_threads <= 1 || indices.size() < 2) {
        for (uint32_t idx : indices) {
            fn(idx);
        }
        return;
    }

    unsigned threads = std::min<unsigned>(num_threads, static_cast<unsigned>(indices.size()));
    std::vector<std::thread> workers;
    workers.reserve(threads);

    std::size_t total = indices.size();
    std::size_t base = total / threads;
    std::size_t rem = total % threads;
    std::size_t begin = 0;

    for (unsigned t = 0; t < threads; ++t) {
        std::size_t chunk = base + (t < rem ? 1 : 0);
        std::size_t end = begin + chunk;
        workers.emplace_back([begin, end, &indices, &fn]() {
            for (std::size_t i = begin; i < end; ++i) {
                fn(indices[i]);
            }
        });
        begin = end;
    }

    for (auto& th : workers) {
        th.join();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <anns.csv> <rov_asns.csv> [threads]\n";
        return 1;
    }

    unsigned num_threads = 1;
    if (argc >= 4) {
        unsigned tmp = 0;
        std::string_view tv(argv[3]);
        auto res = std::from_chars(tv.data(), tv.data() + tv.size(), tmp);
        if (res.ec == std::errc() && tmp > 0) {
            num_threads = std::min<unsigned>(tmp, 16u);
        }
    }
    unsigned hw = std::thread::hardware_concurrency();
    if (hw > 0) {
        num_threads = std::min<unsigned>(num_threads, hw);
    }

    ASGraph graph;
    if (!graph.load_from_caida("data/as-rel.txt.bz2")) {
        std::cerr << "Error loading CAIDA data\n";
        return 1;
    }

    if (graph.detect_cycles()) {
        std::cerr << "Cycle detected in AS relationships. Exiting.\n";
        return 1;
    }

    auto ranks = graph.flatten_ranks();
    std::vector<uint32_t> all_indices(graph.nodes.size());
    for (uint32_t i = 0; i < all_indices.size(); ++i) {
        all_indices[i] = i;
    }

    // Reserve some space for prefix dictionaries to reduce rehashing
    g_prefix_to_id.reserve(1024);
    g_id_to_prefix.reserve(1024);

    std::unordered_set<ASN> rov_asns;
    std::ifstream rov_file(argv[2]);
    if (rov_file.is_open()) {
        std::string line;
        while (std::getline(rov_file, line)) {
            if (line.empty() || line[0] == '#') continue;
            ASN asn = 0;
            std::string_view v(line.data(), line.size());
            auto first_non_ws = v.find_first_not_of(" \t\r\n");
            if (first_non_ws == std::string_view::npos) continue;
            auto last_non_ws = v.find_last_not_of(" \t\r\n");
            v = v.substr(first_non_ws, last_non_ws - first_non_ws + 1);
            auto res = std::from_chars(v.data(), v.data() + v.size(), asn);
            if (res.ec != std::errc()) continue;
            rov_asns.insert(asn);
        }
        rov_file.close();
    }

    std::vector<BGPState> states(graph.nodes.size());
    for (uint32_t idx = 0; idx < graph.nodes.size(); ++idx) {
        ASNode& node = graph.nodes[idx];
        ASN asn = node.asn;
        BGPState& st = states[idx];
        st.rib.reserve(64);
        st.recv_queue.reserve(64);
        st.is_rov = (rov_asns.count(asn) > 0);
        node.state = &st;
    }

    std::ifstream ann_file(argv[1]);
    if (!ann_file.is_open()) {
        std::cerr << "Failed to open announcements file: " << argv[1] << "\n";
        return 1;
    }

    std::string line;
    std::getline(ann_file, line);
    while (std::getline(ann_file, line)) {
        if (line.empty()) continue;
        // Expected format: ASN,prefix,rov_invalid
        size_t first_comma = line.find(',');
        if (first_comma == std::string::npos) continue;
        size_t second_comma = line.find(',', first_comma + 1);
        if (second_comma == std::string::npos) continue;

        std::string_view asn_view(line.data(), first_comma);
        std::string_view prefix_view(line.data() + first_comma + 1, second_comma - first_comma - 1);
        std::string_view rov_view(line.data() + second_comma + 1, line.size() - second_comma - 1);

        // Trim whitespace from views
        auto trim = [](std::string_view v) {
            auto start = v.find_first_not_of(" \t\r\n");
            if (start == std::string_view::npos) return std::string_view();
            auto end = v.find_last_not_of(" \t\r\n");
            return v.substr(start, end - start + 1);
        };

        asn_view = trim(asn_view);
        prefix_view = trim(prefix_view);
        rov_view = trim(rov_view);
        if (asn_view.empty() || prefix_view.empty() || rov_view.empty()) continue;

        ASN origin_asn = 0;
        auto asn_res = std::from_chars(asn_view.data(), asn_view.data() + asn_view.size(), origin_asn);
        if (asn_res.ec != std::errc()) continue;

        bool rov_invalid = (rov_view == "True" || rov_view == "true" || rov_view == "1");

        ASNode* origin_node = graph.get_node_by_asn(origin_asn);
        if (!origin_node || !origin_node->state) continue;

        // Materialize prefix string once for the dictionary
        std::string prefix(prefix_view);
        PrefixID pid = get_prefix_id(prefix);
        Announcement ann;
        ann.prefix_id = pid;
        ann.as_path.reserve(16);
        ann.as_path.push_back(origin_asn);
        ann.next_hop = origin_asn;
        ann.rel = ORIGIN;
        ann.rov_invalid = rov_invalid;

        origin_node->state->rib[pid] = std::move(ann);
    }
    ann_file.close();

    // Phase 1: UP (customers -> providers)
    for (size_t r = 0; r < ranks.size(); ++r) {
        const auto& idxs = ranks[r];
        parallel_for_indices(idxs, num_threads, [&](uint32_t idx) {
            if (idx >= graph.nodes.size()) return;
            ASNode& node = graph.nodes[idx];
            if (!node.state) return;
            ASN asn = node.asn;
            bgp_process_queue(*node.state, asn);
        });
        // Sequential sending to avoid concurrent writes to neighbor queues
        for (uint32_t idx : idxs) {
            if (idx >= graph.nodes.size()) continue;
            ASNode& node = graph.nodes[idx];
            if (!node.state) continue;
            send_announcements(graph, idx, node.providers, PROV);
        }
    }

    // Phase 2: PEERS
    for (uint32_t idx : all_indices) {
        ASNode& node = graph.nodes[idx];
        if (!node.state) continue;
        send_announcements(graph, idx, node.peers, PEER);
    }
    parallel_for_indices(all_indices, num_threads, [&](uint32_t idx) {
        ASNode& node = graph.nodes[idx];
        if (!node.state) return;
        bgp_process_queue(*node.state, node.asn);
    });

    // Phase 3: DOWN (providers -> customers)
    for (int r = static_cast<int>(ranks.size()) - 1; r >= 0; --r) {
        const auto& idxs = ranks[static_cast<size_t>(r)];
        for (uint32_t idx : idxs) {
            if (idx >= graph.nodes.size()) continue;
            ASNode& node = graph.nodes[idx];
            if (!node.state) continue;
            send_announcements(graph, idx, node.customers, CUST);
        }
        parallel_for_indices(idxs, num_threads, [&](uint32_t idx) {
            if (idx >= graph.nodes.size()) return;
            ASNode& node = graph.nodes[idx];
            if (!node.state) return;
            bgp_process_queue(*node.state, node.asn);
        });
    }

    std::cout << "asn,prefix,as_path\n";
    for (const auto& node : graph.nodes) {
        if (!node.state) continue;
        ASN asn = node.asn;
        for (const auto& [prefix_id, ann] : node.state->rib) {
            if (prefix_id >= g_id_to_prefix.size()) continue;
            const std::string& prefix = g_id_to_prefix[prefix_id];
            std::cout << asn << "," << prefix << ",";
            for (size_t i = 0; i < ann.as_path.size(); ++i) {
                if (i > 0) std::cout << "-";
                std::cout << ann.as_path[i];
            }
            std::cout << "\n";
        }
    }

    return 0;
}

