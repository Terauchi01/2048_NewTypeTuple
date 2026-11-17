#ifndef WEIGHT_PROMOTION_H
#define WEIGHT_PROMOTION_H
#include <stdexcept>
#include <string>
namespace weight_promotion {
enum class Mode { none, control, promote };
inline Mode active_mode = Mode::none;
inline bool applied = false;
inline Mode parse_mode(const std::string& text) {
  if (text == "none") return Mode::none;
  if (text == "control") return Mode::control;
  if (text == "promote") return Mode::promote;
  throw std::invalid_argument("promotion mode must be none, control or promote");
}
inline const char* mode_name(Mode mode) {
  switch (mode) {
    case Mode::none: return "none";
    case Mode::control: return "control";
    case Mode::promote: return "promote";
  }
  throw std::invalid_argument("invalid promotion mode");
}
// Must be called only once while all learning workers are stopped. Only stage1
// is altered; stage0 arrays and RNG are unchanged. TC history is reset equally
// in control/promote. Promote copies update masks with weights so exporting an
// inherited learned feature does not incorrectly zero it.
void apply();
}
#endif
