#ifndef ACTION_SEARCH_H
#define ACTION_SEARCH_H
#include "full_spawn_target.h"
namespace action_search {
enum class Policy { greedy, expectimax };
inline Policy active_policy=Policy::greedy;
inline Policy parse(const std::string& s) {
  if(s=="greedy") return Policy::greedy;
  if(s=="expectimax") return Policy::expectimax;
  throw std::invalid_argument("action policy must be greedy or expectimax");
}
inline const char* name(Policy p) {
  switch(p) {case Policy::greedy:return "greedy";case Policy::expectimax:return "expectimax";}
  throw std::invalid_argument("invalid action policy");
}
// The producing move's reward is added by the caller exactly once.
template<class Move,class Value>
double continuation(const board_t& after,Move&& move,Value&& value) {
  return active_policy==Policy::greedy ? value(after) : full_spawn::compute(after,move,value);
}
}
#endif
