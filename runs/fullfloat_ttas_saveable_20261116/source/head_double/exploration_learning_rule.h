#ifndef EXPLORATION_LEARNING_RULE_H
#define EXPLORATION_LEARNING_RULE_H

#include <cmath>
#include <type_traits>
#include <stdexcept>
#include <string>

// Legacy VSE baseline: alpha=.5 and TC statistics are updated before the
// coherence ratio is applied. This deliberately differs from Guei prior-ratio TC.
namespace learning_rule {
enum class Mode { tc, td, td_then_tc };
enum class Rule { tc, td };
struct Schedule {
  Mode mode = Mode::tc;
  long long switch_step = 0;
  static constexpr double base_rate = .5;
  static constexpr double tc_rate = .5;
  void validate() const {
    if (switch_step < 0 || (mode != Mode::td_then_tc && switch_step != 0))
      throw std::invalid_argument("invalid exploration learning schedule");
  }
  Rule at(long long steps) const {
    if (steps < 0) throw std::invalid_argument("negative completed steps");
    return mode == Mode::tc || (mode == Mode::td_then_tc && steps >= switch_step)
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
inline Schedule active_schedule;
inline thread_local Rule batch_rule = Rule::tc;
template<class T> inline void update(T& weight, T& error_sum, T& absolute_error_sum,
                   std::type_identity_t<T> delta, Rule rule) {
#ifdef NT9_FLOAT32_STORAGE
  if constexpr (std::is_same_v<T,float>) {
    const float d=static_cast<float>(delta);
    if(rule==Rule::tc) {
      absolute_error_sum += std::fabs(d);
      error_sum += d;
      weight += d * .5f * std::fabs(error_sum) / absolute_error_sum;
    } else weight += .5f*d;
    return;
  }
#endif
  if (rule == Rule::tc) {
    absolute_error_sum += std::fabs(delta);
    error_sum += delta;
    weight += delta * Schedule::tc_rate * std::fabs(error_sum) / absolute_error_sum;
  } else {
    weight += Schedule::base_rate * delta;
  }
}
}  // namespace learning_rule
#endif
