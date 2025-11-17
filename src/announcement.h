#pragma once
#include <string>
#include <vector>
#include <cstdint>

typedef uint32_t ASN;

typedef uint32_t PrefixID;

// Relationship types for BGP announcements
enum Rel { ORIGIN = 3, CUST = 2, PEER = 1, PROV = 0 };

// Structure for BGP announcements
struct Announcement {
  PrefixID prefix_id;    // Identifier for destination IP prefix
  std::vector<ASN> as_path;  // Path of ASNs
  ASN next_hop;          // Next-hop ASN
  Rel rel;               // Relationship type (provider/customer/peer/origin)
  bool rov_invalid;      // True if ROV marks this announcement as invalid
  // Comparison operator for prioritization
  bool operator>(const Announcement& other) const {
    if (rel != other.rel) return rel > other.rel;
    if (as_path.size() != other.as_path.size()) return as_path.size() < other.as_path.size();
    return next_hop < other.next_hop;
  }
};