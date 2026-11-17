#ifndef GUEI_LEARNING_RULE_SCHEDULE_H
#define GUEI_LEARNING_RULE_SCHEDULE_H

// Opt-in experiment 1. Original learner files do not include this header.
#include <cmath>
#include <stdexcept>
#include <string>

namespace learning_rule {
enum class Mode { tc, td, td_then_tc };
enum class Rule { tc, td };

struct Schedule {
  Mode mode = Mode::tc;
  long long switch_step = 0;
  static constexpr double base_rate = 0.1;
  static constexpr double tc_rate = 1.0;

  void validate() const {
    if (switch_step < 0 || (mode != Mode::td_then_tc && switch_step != 0))
      throw std::invalid_argument("switch_step must be nonnegative and only used with td-tc");
  }

  // Latch this result once for an entire gameEnd backward-update batch.
  Rule at(long long completed_steps) const {
    if (completed_steps < 0) throw std::invalid_argument("negative completed_steps");
    return mode == Mode::tc || (mode == Mode::td_then_tc && completed_steps >= switch_step)
               ? Rule::tc : Rule::td;
  }
};

inline Mode parse_mode(const std::string& text) {
  if (text == "tc") return Mode::tc;
  if (text == "td") return Mode::td;
  if (text == "td-tc") return Mode::td_then_tc;
  throw std::invalid_argument("learning mode must be tc, td or td-tc");
}

inline const char* mode_name(Mode mode) {
  switch (mode) {
    case Mode::tc: return "tc";
    case Mode::td: return "td";
    case Mode::td_then_tc: return "td-tc";
  }
  throw std::invalid_argument("invalid learning mode");
}

// Configured before workers start, immutable during learning. Each worker
// latches batch_rule before its gameEnd backward sweep.
inline Schedule active_schedule;
inline thread_local Rule batch_rule = Rule::tc;

// Guei/TDL2048-inspired port: use the coherence ratio before adding this
// update to its statistics. Normalization of delta is unchanged from VSE.
inline void update(double& weight, double& error_sum, double& absolute_error_sum,
                   double delta, Rule rule) {
  if (rule == Rule::tc) {
    const double ratio = absolute_error_sum > 0.0
        ? std::fabs(error_sum) / absolute_error_sum : 1.0;
    weight += delta * Schedule::tc_rate * ratio;
    error_sum += delta;
    absolute_error_sum += std::fabs(delta);
  } else {
    weight += delta * Schedule::base_rate;
  }
}
}  // namespace learning_rule
#endif
