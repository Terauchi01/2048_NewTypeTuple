#ifndef EXPLORATION_POLICY_H
#define EXPLORATION_POLICY_H

#include "game2048.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace exploration {
#ifdef NT9_FLOAT32_STORAGE
using scalar_t = float;
#else
using scalar_t = double;
#endif
enum class Mode { greedy, tc_ucb };
enum class UcbFormula { standard, paper, source };
enum class Teacher { greedy_max, legacy };
inline constexpr scalar_t statistic_epsilon = scalar_t(1e-12);
inline constexpr scalar_t score_scale_floor = scalar_t(1);
inline constexpr const char* formula_specification =
    "standard=sqrt(log1p(sum_minA)/max(minA,1e-12));"
    "paper=sqrt(log(max(minA,1))/max(sum_minA,1e-12));"
    "source=sqrt(log1p(sum_minA)/(1+minA));"
    "paper_score_scale=max(1,max_legal_reward_plus_V);"
    "source_score_scale=max(1,max_legal_reward_plus_V);"
    "standard_score_scale=max(1,max_abs_legal_reward_plus_V);lowest_direction_ties";

inline const char* mode_name(Mode mode) {
  return mode == Mode::greedy ? "greedy" : "tc-ucb";
}
inline const char* formula_name(UcbFormula formula) {
  switch (formula) {
    case UcbFormula::standard: return "standard";
    case UcbFormula::paper: return "paper";
    case UcbFormula::source: return "source";
  }
  return "unknown";
}
inline const char* teacher_name(Teacher teacher) {
  return teacher == Teacher::greedy_max ? "greedy-max" : "legacy";
}
inline Mode parse_mode(const std::string& text) {
  if (text == "greedy") return Mode::greedy;
  if (text == "tc-ucb") return Mode::tc_ucb;
  throw std::invalid_argument("behavior must be greedy or tc-ucb");
}
inline UcbFormula parse_formula(const std::string& text) {
  if (text == "standard") return UcbFormula::standard;
  if (text == "paper") return UcbFormula::paper;
  if (text == "source") return UcbFormula::source;
  throw std::invalid_argument("UCB formula must be standard, paper or source");
}
inline Teacher parse_teacher(const std::string& text) {
  if (text == "greedy-max") return Teacher::greedy_max;
  if (text == "legacy") return Teacher::legacy;
  throw std::invalid_argument("teacher must be greedy-max or legacy");
}

struct Config {
  Mode mode = Mode::greedy;
  UcbFormula formula = UcbFormula::paper;
  Teacher teacher = Teacher::greedy_max;
  scalar_t c = 0;
  long long warmup_steps = 0;
  long long decay_end_steps = 0;  // Zero means a constant coefficient after warmup.
  void validate() const {
    if ((mode != Mode::greedy && mode != Mode::tc_ucb) ||
        (formula != UcbFormula::standard && formula != UcbFormula::paper &&
         formula != UcbFormula::source) ||
        (teacher != Teacher::greedy_max && teacher != Teacher::legacy) ||
        !std::isfinite(c) || c < 0 || warmup_steps < 0 || decay_end_steps < 0 ||
        (decay_end_steps != 0 && decay_end_steps <= warmup_steps))
      throw std::invalid_argument("invalid exploration coefficient/schedule");
  }
  scalar_t coefficient(long long steps) const {
    if (steps < 0) throw std::invalid_argument("negative exploration step");
    if (mode == Mode::greedy || steps < warmup_steps) return 0;
    if (decay_end_steps == 0) return c;
    if (steps >= decay_end_steps) return 0;
    return c * (scalar_t(decay_end_steps - steps) /
                scalar_t(decay_end_steps - warmup_steps));
  }
};
inline Config config;

struct Decision {
  std::array<bool, 4> legal{};
  std::array<scalar_t, 4> raw_score{};  // Immediate reward + unbonused V(afterstate).
  std::array<scalar_t, 4> minA{};
  std::array<scalar_t, 4> bonus{};      // Normalized behavior-score units only.
  std::array<scalar_t, 4> behavior_score{};
  int chosen = -1;
  int greedy = -1;
  int stage = 0;
  int progress = 0;
  int worker_id = -1;
  long long completed_steps = 0;
  scalar_t effective_c = 0;
  scalar_t score_scale = 1;
  int paper_log_floor_count = 0;
  bool paper_denominator_floor = false;
  bool score_scale_floor = false;
};
struct Context {
  std::mt19937* exploration_rng = nullptr;
  int worker_id = -1;
  long long completed_steps = 0;
  Decision decision;
};
inline thread_local Context worker_context;
inline void bind_worker(std::mt19937* rng, int worker_id) {
  worker_context.exploration_rng = rng;
  worker_context.worker_id = worker_id;
}
inline void set_step(long long steps) {
  if (steps < 0) throw std::invalid_argument("negative exploration step");
  worker_context.completed_steps = steps;
}
inline const Decision& last_decision() { return worker_context.decision; }

// TC-UCB is deterministic here; the driver's distinct exploration RNG remains
// owned/serialized by the driver and is not consumed by greedy or TC-UCB ties.
inline Decision choose(const std::array<bool, 4>& legal,
                       const std::array<scalar_t, 4>& score,
                       const std::array<scalar_t, 4>& minA) {
  Decision result;
  result.legal = legal;
  result.raw_score = score;
  result.minA = minA;
  result.worker_id = worker_context.worker_id;
  result.completed_steps = worker_context.completed_steps;
  result.effective_c = config.coefficient(result.completed_steps);
  scalar_t sum = 0;
  scalar_t maximum_legal_score = -std::numeric_limits<scalar_t>::infinity();
  scalar_t maximum_absolute_score = 0;
  for (int action = 0; action < 4; ++action) {
    result.behavior_score[action] = -std::numeric_limits<scalar_t>::infinity();
    if (!legal[action]) continue;
    if (!std::isfinite(score[action]) || !std::isfinite(minA[action]) || minA[action] < 0)
      throw std::runtime_error("non-finite action value or invalid TC statistic");
    if (result.greedy < 0 || score[action] > score[result.greedy]) result.greedy = action;
    maximum_legal_score = std::max(maximum_legal_score, score[action]);
    maximum_absolute_score = std::max(maximum_absolute_score, std::fabs(score[action]));
    sum += minA[action];
  }
  if (result.greedy < 0) return result;
  if (!std::isfinite(sum)) throw std::runtime_error("TC statistic sum overflow");
  const scalar_t unguarded_scale = config.formula == UcbFormula::standard
      ? maximum_absolute_score : maximum_legal_score;
  result.score_scale = std::max(score_scale_floor, unguarded_scale);
  if (result.effective_c != 0) {
    result.score_scale_floor = unguarded_scale < score_scale_floor;
    result.paper_denominator_floor = config.formula == UcbFormula::paper &&
        sum < statistic_epsilon;
  }
  result.chosen = result.greedy;
  for (int action = 0; action < 4; ++action) {
    if (!legal[action]) continue;
    if (result.effective_c == 0) {
      result.behavior_score[action] = score[action];
      continue;  // Preserve exact raw greedy ranking, including near ties.
    }
    const scalar_t term = config.formula == UcbFormula::standard
        ? std::log1p(sum) / std::max(minA[action], statistic_epsilon)
        : config.formula == UcbFormula::source
        ? std::log1p(sum) / (scalar_t(1) + minA[action])
        : std::log(std::max(minA[action], scalar_t(1))) / std::max(sum, statistic_epsilon);
    if (config.formula == UcbFormula::paper && minA[action] < scalar_t(1))
      ++result.paper_log_floor_count;
    result.bonus[action] = result.effective_c * std::sqrt(term);
    result.behavior_score[action] = score[action] / result.score_scale + result.bonus[action];
    if (!std::isfinite(result.behavior_score[action]))
      throw std::runtime_error("non-finite exploration score");
  }
  if (result.effective_c != 0) {
    result.chosen = -1;
    for (int action = 0; action < 4; ++action)
      if (legal[action] && (result.chosen < 0 ||
          result.behavior_score[action] > result.behavior_score[result.chosen]))
        result.chosen = action;
  }
  return result;
}

// Implemented by the model. Recomputed at backward-update time on the actual
// next state; no exploration bonus enters this target. No legal move => zero.
scalar_t greedy_state_target(const board_t& state);
void learn_greedy_state_target(const board_t& before, const board_t& next_state);
}  // namespace exploration
#endif
