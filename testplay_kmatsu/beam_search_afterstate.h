#ifndef BEAM_SEARCH_AFTERSTATE_H
#define BEAM_SEARCH_AFTERSTATE_H

#include "beam_search.h"

namespace beam2048 {

// Expectimax-style beam search that evaluates and prunes only afterstates.
// Depth counts player afterstates; chance spawns do not consume a ply.
Result search_afterstate(const board_t& board, const Config& config,
                         const Evaluator& evaluator);

}  // namespace beam2048

#endif  // BEAM_SEARCH_AFTERSTATE_H
