#pragma once
#include "announcement.h"
#include <unordered_map>
#include <vector>

// Flat BGP state per AS (no virtual calls)
struct BGPState {
  std::unordered_map<PrefixID, Announcement> rib;  // Local Routing Information Base indexed by prefix ID
  std::unordered_map<PrefixID, Announcement> recv_queue;  // Best received announcement per prefix ID (pending)
  bool is_rov = false;  // if true, drop rov_invalid announcements on receive
};

// Enqueue a received announcement into the per-prefix queue
void bgp_receive(BGPState& state, PrefixID prefix_id, const Announcement& ann);

// Process receive queues and update the local RIB according to selection rules
void bgp_process_queue(BGPState& state, ASN self_asn);