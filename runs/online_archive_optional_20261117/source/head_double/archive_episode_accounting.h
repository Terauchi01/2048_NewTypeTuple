#pragma once

#include <limits>
#include <stdexcept>

// A saved afterstate has already completed source_turn actions. Local turn 1
// is the next action; the restart/history indices remain entirely local.
namespace archive_episode_accounting {
inline long long add(long long source, long long local) {
  if(source<0||local<0||source>std::numeric_limits<long long>::max()-local)
    throw std::overflow_error("invalid archive episode accounting");
  return source+local;
}
inline int placement_point(long long absolute_turn) {
  return absolute_turn==7200?0:absolute_turn==14500?1:-1;
}
}
