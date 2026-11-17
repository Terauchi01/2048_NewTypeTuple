#ifndef EXPLORATION_CANDIDATE_CACHE_H
#define EXPLORATION_CANDIDATE_CACHE_H
#include <cstddef>
#include <cstdint>
namespace exploration_candidate_cache {
// Immutable while workers run. This cache stores indices, never learned values.
inline bool enabled = false;
inline std::size_t budget_bytes = 16u * 1024u * 1024u;
inline constexpr const char* specification =
    "bounded_per_worker_fixed_turn_array;all_legal_physical_indices;live_value_teacher;"
    "duplicate_order_preserved;overflow_recompute;transient_no_checkpoint_v1";
struct Counters {
  std::uint64_t stored_turns=0, fallback_turns=0, stored_legal_candidates=0;
  std::size_t allocated_bytes=0, capacity_turns=0;
};
Counters counters();
}
#endif
