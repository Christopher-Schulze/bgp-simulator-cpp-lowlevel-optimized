// ASGraph - graph of AS relationships from CAIDA data
// Loads bz2 file, creates nodes with providers/customers/peers, cycle detection and rank flattening for propagation
#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

typedef uint32_t ASN;

enum RelType { P2C = 0, C2P = -1, P2P = 1 };

struct BGPState;

struct ASNode {
    ASN asn = 0;
    std::vector<uint32_t> providers;   // indices of provider ASNs
    std::vector<uint32_t> customers;   // indices of customer ASNs
    std::vector<uint32_t> peers;       // indices of peer ASNs
    BGPState* state = nullptr;         // pointer into per-AS BGP state
    int rank = -1;
};

class ASGraph {
public:
    std::vector<ASNode> nodes;                     // index-based storage
    std::unordered_map<ASN, uint32_t> asn_to_index;  // maps ASN -> node index
    bool load_from_caida(const std::string& bz2_path);
    bool detect_cycles();
    std::vector<std::vector<uint32_t>> flatten_ranks();

    ASNode* get_node_by_asn(ASN asn);
    const ASNode* get_node_by_asn(ASN asn) const;

private:
    bool has_cycle(uint32_t index, std::vector<int>& visit);
};