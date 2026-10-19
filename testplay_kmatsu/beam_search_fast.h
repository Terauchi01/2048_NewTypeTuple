#ifndef BEAM_SEARCH_FAST_H
#define BEAM_SEARCH_FAST_H

#include "beam_search.h"

namespace beam2048 {

// Optimized implementation kept separate from search() so results can be
// compared against the stable-sort baseline without changing its behavior.
Result search_fast(const board_t& board, const Config& config,
                   const Evaluator& evaluator);

}  // namespace beam2048

#endif  // BEAM_SEARCH_FAST_H
