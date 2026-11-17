#ifndef SUCCESS_PROBABILITY_H
#define SUCCESS_PROBABILITY_H

#include "game2048.h"
#include "full_spawn_target.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace success_probability {
enum class Objective { score, reach32k };
inline Objective active_objective = Objective::score;
inline double initial_probability = 1.0;
inline thread_local bool terminal_success = false;
inline Objective parse_objective(const std::string& text) {
  if (text == "score") return Objective::score;
  if (text == "reach32k") return Objective::reach32k;
  throw std::invalid_argument("objective must be score or reach32k");
}
inline const char* objective_name(Objective value) {
  return value == Objective::score ? "score" : "reach32k";
}
inline bool enabled() { return active_objective == Objective::reach32k; }
inline bool succeeded(const board_t& board) {
  for (int v : board) if (v >= 15) return true;
  return false;
}
inline void validate_initial(double value) {
  if (!std::isfinite(value) || value < 0 || value > 1)
    throw std::invalid_argument("probability initial value must be finite and within [0,1]");
}
inline double estimate(const board_t& board, double raw) {
  // Linear N-tuple estimates can leave [0,1]. Preserve the learned function;
  // evaluation reports out-of-range predictions instead of silently clipping.
  return enabled() && succeeded(board) ? 1.0 : raw;
}
inline double action_value(const board_t& after, double value, int reward) {
  return enabled() ? estimate(after, value) : reward + value;
}
inline double sampled_target(double next_value, int reward) {
  return enabled() ? next_value : reward + next_value;
}
inline double terminal_target() { return enabled() && terminal_success ? 1.0 : 0.0; }
template <class Move, class Value>
double full_target(const board_t& after, Move&& move, Value&& value) {
  if (succeeded(after)) return 1.0;
  return full_spawn::compute(after,
      [&](const board_t& board, board_t& next, move_dir direction) {
        return move(board, next, direction) < 0 ? -1 : 0;
      },
      [&](const board_t& next) {
        return succeeded(next) ? 1.0 : value(next);
      });
}
} // namespace success_probability
#endif
