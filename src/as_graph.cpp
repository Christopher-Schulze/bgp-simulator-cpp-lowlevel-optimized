#include "as_graph.h"
#include <bzlib.h>
#include <fstream>
#include <iostream>
#include <stack>
#include <queue>
#include <string_view>
#include <charconv>

// Loads CAIDA AS relationships from a compressed bz2 file
// File format: ASN1 ASN2 | type (P2C=0 C2P=-1 P2P=1), ignores the source column
// Reads the file as a stream to keep memory usage low
bool ASGraph::load_from_caida(const std::string& bz2_path) {
    // Open the bz2 file for reading
    FILE* fp = fopen(bz2_path.c_str(), "rb");
    if (!fp) {
        std::cerr << "Failed to open file: " << bz2_path << std::endl;
        return false;
    }

    nodes.clear();
    asn_to_index.clear();

    int bzerr = BZ_OK;
    BZFILE* bzf = BZ2_bzReadOpen(&bzerr, fp, /*verbosity*/0, /*small*/0, nullptr, 0);
    if (!bzf || bzerr != BZ_OK) {
        std::cerr << "Error opening bz2 file" << std::endl;
        if (bzf) {
            BZ2_bzReadClose(&bzerr, bzf);
        }
        fclose(fp);
        return false;
    }

    char buf[8192];
    size_t total_lines = 0;
    std::string leftover;

    auto get_index = [this](ASN asn) -> uint32_t {
        auto it = asn_to_index.find(asn);
        if (it != asn_to_index.end()) return it->second;
        uint32_t idx = static_cast<uint32_t>(nodes.size());
        nodes.emplace_back();
        ASNode& n = nodes.back();
        n.asn = asn;
        n.providers.reserve(8);
        n.customers.reserve(8);
        n.peers.reserve(8);
        asn_to_index.emplace(asn, idx);
        return idx;
    };

    auto trim = [](std::string_view v) {
        auto start = v.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) return std::string_view();
        auto end = v.find_last_not_of(" \t\r\n");
        return v.substr(start, end - start + 1);
    };

    auto parse_line = [&](std::string_view v) {
        v = trim(v);
        if (v.empty() || v[0] == '#') return;

        // Expected format: ASN1|ASN2|type (type: 0, -1, 1)
        size_t first_sep = v.find('|');
        if (first_sep == std::string_view::npos) return;
        size_t second_sep = v.find('|', first_sep + 1);
        if (second_sep == std::string_view::npos) return;

        std::string_view asn1_view = trim(v.substr(0, first_sep));
        std::string_view asn2_view = trim(v.substr(first_sep + 1, second_sep - first_sep - 1));
        std::string_view type_view = trim(v.substr(second_sep + 1));
        if (asn1_view.empty() || asn2_view.empty() || type_view.empty()) return;

        ASN asn1 = 0;
        ASN asn2 = 0;
        int type = 0;

        auto res1 = std::from_chars(asn1_view.data(), asn1_view.data() + asn1_view.size(), asn1);
        if (res1.ec != std::errc()) return;
        auto res2 = std::from_chars(asn2_view.data(), asn2_view.data() + asn2_view.size(), asn2);
        if (res2.ec != std::errc()) return;
        auto res3 = std::from_chars(type_view.data(), type_view.data() + type_view.size(), type);
        if (res3.ec != std::errc()) return;

        uint32_t idx1 = get_index(asn1);
        uint32_t idx2 = get_index(asn2);
        ASNode& n1 = nodes[idx1];
        ASNode& n2 = nodes[idx2];

        if (type == P2C) {
            n1.customers.emplace_back(idx2);
            n2.providers.emplace_back(idx1);
        } else if (type == C2P) {
            n2.customers.emplace_back(idx1);
            n1.providers.emplace_back(idx2);
        } else if (type == P2P) {
            n1.peers.emplace_back(idx2);
            n2.peers.emplace_back(idx1);
        }
        ++total_lines;
    };

    while (true) {
        int nread = BZ2_bzRead(&bzerr, bzf, buf, static_cast<int>(sizeof(buf)));
        if (bzerr != BZ_OK && bzerr != BZ_STREAM_END) {
            std::cerr << "Error reading bz2 file" << std::endl;
            BZ2_bzReadClose(&bzerr, bzf);
            fclose(fp);
            return false;
        }
        
        if (nread > 0) {
            std::string chunk = leftover + std::string(buf, nread);
            leftover.clear();
            
            size_t pos = 0;
            while (pos < chunk.size()) {
                size_t nl = chunk.find('\n', pos);
                if (nl == std::string::npos) {
                    leftover = chunk.substr(pos);
                    break;
                }

                std::size_t len = nl - pos;
                parse_line(std::string_view(chunk.data() + pos, len));
                pos = nl + 1;
            }
        }
        
        if (bzerr == BZ_STREAM_END) {
            if (!leftover.empty() && leftover[0] != '#') {
                parse_line(std::string_view(leftover.data(), leftover.size()));
            }
            break;
        }
    }

    BZ2_bzReadClose(&bzerr, bzf);
    fclose(fp);
    std::cerr << "Loaded: " << nodes.size() << " nodes, " << total_lines << " relationships" << std::endl;
    return true;
}

bool ASGraph::detect_cycles() {
    // DFS for cycles in provider/customer relationships (ignore peers)
    std::vector<int> visit(nodes.size(), 0);
    for (uint32_t idx = 0; idx < nodes.size(); ++idx) {
        if (visit[idx] == 0) {
            if (has_cycle(idx, visit)) return true;
        }
    }
    return false;
}

bool ASGraph::has_cycle(uint32_t index, std::vector<int>& visit) {
    visit[index] = 1;
    ASNode& node = nodes[index];
    for (uint32_t prov_idx : node.providers) {
        if (visit[prov_idx] == 1) return true;
        if (visit[prov_idx] == 0 && has_cycle(prov_idx, visit)) return true;
    }
    visit[index] = 2;
    return false;
}

std::vector<std::vector<uint32_t>> ASGraph::flatten_ranks() {
    std::vector<int> rank(nodes.size(), -1);
    std::queue<uint32_t> q;
    for (uint32_t idx = 0; idx < nodes.size(); ++idx) {
        if (nodes[idx].customers.empty()) {
            rank[idx] = 0;
            nodes[idx].rank = 0;
            q.push(idx);
        }
    }
    int cur_rank = 0;
    while (!q.empty()) {
        int sz = static_cast<int>(q.size());
        for (int i = 0; i < sz; ++i) {
            uint32_t u = q.front();
            q.pop();
            for (uint32_t prov_idx : nodes[u].providers) {
                if (rank[prov_idx] == -1) {
                    int r = cur_rank + 1;
                    rank[prov_idx] = r;
                    nodes[prov_idx].rank = r;
                    q.push(prov_idx);
                }
            }
        }
        ++cur_rank;
    }
    int max_rank = -1;
    for (int r : rank) {
        if (r > max_rank) max_rank = r;
    }
    std::vector<std::vector<uint32_t>> ranks;
    if (max_rank >= 0) {
        ranks.resize(static_cast<std::size_t>(max_rank + 1));
        for (uint32_t idx = 0; idx < nodes.size(); ++idx) {
            if (rank[idx] >= 0) {
                ranks[static_cast<std::size_t>(rank[idx])].push_back(idx);
            }
        }
    }
    return ranks;
}

ASNode* ASGraph::get_node_by_asn(ASN asn) {
    auto it = asn_to_index.find(asn);
    if (it == asn_to_index.end()) return nullptr;
    uint32_t idx = it->second;
    if (idx >= nodes.size()) return nullptr;
    return &nodes[idx];
}

const ASNode* ASGraph::get_node_by_asn(ASN asn) const {
    auto it = asn_to_index.find(asn);
    if (it == asn_to_index.end()) return nullptr;
    uint32_t idx = it->second;
    if (idx >= nodes.size()) return nullptr;
    return &nodes[idx];
}