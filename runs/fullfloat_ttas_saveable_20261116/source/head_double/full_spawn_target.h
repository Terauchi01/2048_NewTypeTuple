#ifndef FULL_SPAWN_TARGET_H
#define FULL_SPAWN_TARGET_H

#include "game2048.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace full_spawn {

enum class Target { sampled, full_spawn };
inline Target active_target = Target::sampled;

inline Target parse_target(const std::string& text) {
  if (text == "sampled") return Target::sampled;
  if (text == "full-spawn") return Target::full_spawn;
  throw std::invalid_argument("target must be sampled or full-spawn");
}

inline const char* target_name(Target target) {
  switch (target) {
    case Target::sampled: return "sampled";
    case Target::full_spawn: return "full-spawn";
  }
  throw std::invalid_argument("invalid learning target");
}

// The input is an afterstate: its producing move's reward is already earned.
// Spawn a 2/4 in each empty cell, then maximize the NEXT move's reward plus
// afterstate value. A spawn with no legal moves contributes zero continuation.
// Even the last observed afterstate must use this target: the observed spawn
// can end the game while other possible spawns still permit another move.
template <class Move, class Value>
double compute(const board_t& afterstate, Move&& move, Value&& value) {
  int empty_count = 0;
  for (int cell : afterstate) empty_count += cell == 0;
  if (empty_count == 0)
    throw std::invalid_argument("full-spawn target requires an empty afterstate cell");

  double total = 0;
  for (int cell = 0; cell < 16; ++cell) {
    if (afterstate[cell] != 0) continue;
    for (int exponent = 1; exponent <= 2; ++exponent) {
      board_t spawned;
      std::copy(afterstate, afterstate + 16, spawned);
      spawned[cell] = exponent;
      double best = -std::numeric_limits<double>::infinity();
      bool legal = false;
      for (int direction = 0; direction < 4; ++direction) {
        board_t next_after;
        const int reward = move(spawned, next_after, static_cast<move_dir>(direction));
        if (reward < 0) continue;
        legal = true;
        const double candidate = reward + value(next_after);
        if (candidate > best) best = candidate;
      }
      const double probability = (exponent == 1 ? 0.9 : 0.1) / empty_count;
      total += probability * (legal ? best : 0.0);
    }
  }
  return total;
}

}  // namespace full_spawn

#endif
