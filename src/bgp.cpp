#include "bgp.h"
#include <algorithm>
#include <utility>

// Enqueue a received announcement into the per-prefix queue.
// If ROV is enabled, drop announcements marked as rov_invalid.
void bgp_receive(BGPState& state, PrefixID prefix_id, const Announcement& ann) {
  if (state.is_rov && ann.rov_invalid) {
    return;
  }
  auto it = state.recv_queue.find(prefix_id);
  if (it == state.recv_queue.end()) {
    state.recv_queue.emplace(prefix_id, ann);
  } else {
    // Keep only the best candidate per prefix according to operator>
    if (ann > it->second) {
      it->second = ann;
    }
  }
}

// Processes the receive queue for each prefix:
// Selects the best announcement according to priority
// (relationship > shortest path > lowest next_hop) and updates the local RIB.
void bgp_process_queue(BGPState& state, ASN self_asn) {
  for (auto& [prefix_id, best] : state.recv_queue) {
    if (best.as_path.empty() || best.as_path.front() != self_asn) {
      best.as_path.insert(best.as_path.begin(), self_asn);
    }
    state.rib[prefix_id] = std::move(best);
  }
  state.recv_queue.clear();
}