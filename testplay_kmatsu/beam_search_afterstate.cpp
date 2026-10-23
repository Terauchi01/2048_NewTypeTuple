#include "beam_search_afterstate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace beam2048 {
namespace {

struct ChanceGroup {
  double probability = 0.0;
  double terminal_value = 0.0;
  bool terminal = false;
  std::array<std::size_t, 4> afterstate_children{};
  std::size_t child_count = 0;
};

struct Node {
  Board afterstate{};
  double probability = 0.0;
  double accumulated_reward = 0.0;
  // Heuristic value at this afterstate; used at the horizon and for beam
  // ranking. Internal nodes receive their expectimax value in backup().
  double value = 0.0;
  bool expanded = false;
  std::size_t first_chance = 0;
  std::size_t chance_count = 0;
  std::uint64_t order = 0;
};

using Clock = std::chrono::steady_clock;

bool expired(Clock::time_point deadline, Stats& stats) {
  ++stats.deadline_checks;
  return Clock::now() >= deadline;
}

// A bounded per-thread cache. Each search gets a fresh generation so values
// never leak between evaluators. Keys store all sixteen tile exponents exactly;
// no symmetry assumptions or probabilistic hash matches change the result.
class CachedEvaluator {
  struct Entry {
    std::uint64_t low = 0, high = 0, generation = 0;
    double value = 0;
  };
  struct Storage {
    std::vector<Entry> entries;
    std::uint64_t generation = 0;
  };
  static Storage& storage() {
    static thread_local Storage cache;
    return cache;
  }
  const Evaluator& evaluator_;
  Stats& stats_;
  bool enabled_;
  std::uint64_t generation_;
 public:
  CachedEvaluator(const Evaluator& evaluator, Stats& stats, bool enabled)
      : evaluator_(evaluator), stats_(stats), enabled_(enabled),
        generation_(++storage().generation) {
    if (generation_ == 0) {
      storage().entries.clear();
      generation_ = ++storage().generation;
    }
  }
  double operator()(const Board& board) {
    Entry* entry = nullptr;
    std::uint64_t low = 0, high = 0;
    bool packable = true;
    if (enabled_) {
      for (int i = 0; i < 16; ++i) {
        if (board[i] < 0 || board[i] > 31) { packable = false; break; }
        if (i < 12) low |= static_cast<std::uint64_t>(board[i]) << (5*i);
        else high |= static_cast<std::uint64_t>(board[i]) << (5*(i-12));
      }
      if (packable) {
        auto& cache = storage();
        if (cache.entries.empty()) cache.entries.resize(65536);
        std::uint64_t hash = low ^ (high * 0x9e3779b97f4a7c15ULL);
        hash ^= hash >> 30; hash *= 0xbf58476d1ce4e5b9ULL;
        hash ^= hash >> 27; hash *= 0x94d049bb133111ebULL;
        hash ^= hash >> 31;
        entry = &cache.entries[hash & (cache.entries.size()-1)];
        if (entry->generation == generation_ && entry->low == low && entry->high == high)
          return entry->value;
      }
    }
    const double value = evaluator_(board);
    ++stats_.evaluated_nodes;
    if (!std::isfinite(value))
      throw std::domain_error("beam-search evaluator returned a non-finite value");
    if (entry) *entry = Entry{low, high, generation_, value};
    return value;
  }
};

bool expected_contribution_better(const Node& lhs, const Node& rhs) {
  const double lhs_contribution = lhs.probability * lhs.value;
  const double rhs_contribution = rhs.probability * rhs.value;
  if (lhs_contribution != rhs_contribution) {
    return lhs_contribution > rhs_contribution;
  }
  if (lhs.probability != rhs.probability) {
    return lhs.probability > rhs.probability;
  }
  return lhs.order < rhs.order;
}

bool expand_afterstate_layer(const std::vector<std::size_t>& frontier,
                             std::vector<Node>& nodes,
                             std::vector<ChanceGroup>& chance_groups,
                             const Config& config, CachedEvaluator& evaluator,
                             std::size_t layer_depth,
                             Clock::time_point deadline, Stats& stats,
                             std::uint64_t& order,
                             std::vector<std::size_t>& next_frontier) {
  std::vector<std::size_t> candidates;
  candidates.reserve(frontier.size() * 64);

  for (const std::size_t parent_id : frontier) {
    if (expired(deadline, stats)) return false;
    ++stats.expanded_nodes;
    nodes[parent_id].expanded = true;
    nodes[parent_id].first_chance = chance_groups.size();
    nodes[parent_id].chance_count = 0;
    const Board parent_afterstate = nodes[parent_id].afterstate;
    const double parent_probability = nodes[parent_id].probability;
    const double parent_reward = nodes[parent_id].accumulated_reward;

    const std::vector<Spawn> spawns = generate_spawns(parent_afterstate);
    if (spawns.empty()) {
      // No more random tile can appear. This is a terminal afterstate.
      nodes[parent_id].value = parent_reward;
      continue;
    }

    for (const Spawn& spawn : spawns) {
      if (expired(deadline, stats)) return false;
      ChanceGroup group;
      group.probability = spawn.probability;
      const std::size_t group_id = chance_groups.size();
      chance_groups.push_back(std::move(group));
      ++nodes[parent_id].chance_count;

      for (int direction = UP; direction <= LEFT; ++direction) {
        if (expired(deadline, stats)) return false;
        Board afterstate{};
        const int reward = moveB(spawn.board.data(), afterstate.data(),
                                 static_cast<move_dir>(direction));
        if (reward < 0) continue;

        Node child;
        child.afterstate = afterstate;
        child.probability = parent_probability * spawn.probability;
        child.accumulated_reward = parent_reward +
            static_cast<double>(reward) * config.reward_scale;
        child.value = child.accumulated_reward +
                      evaluator(afterstate);
        child.order = order++;
        const std::size_t child_id = nodes.size();
        nodes.push_back(std::move(child));
        auto& group = chance_groups[group_id];
        group.afterstate_children[group.child_count++] = child_id;
        candidates.push_back(child_id);
        ++stats.generated_nodes;
      }

      // A spawn that leaves no legal move has a terminal value but is not an
      // afterstate, so it is never passed to the evaluator or beam selector.
      if (chance_groups[group_id].child_count == 0) {
        chance_groups[group_id].terminal = true;
        chance_groups[group_id].terminal_value = parent_reward;
      }
    }
  }

  if (expired(deadline, stats)) return false;
  std::vector<double> candidate_values;
  if (config.width_policy == WidthPolicy::Adaptive) {
    candidate_values.reserve(candidates.size());
    for (const std::size_t candidate : candidates) {
      candidate_values.push_back(nodes[candidate].probability *
                                 nodes[candidate].value);
    }
  }
  const std::size_t width =
      policy_width_limit(config, layer_depth, candidate_values);
  ++stats.beam_layers;
  stats.effective_width_sum += width;
  if (stats.min_effective_width == 0 || width < stats.min_effective_width) {
    stats.min_effective_width = width;
  }
  stats.max_effective_width = std::max(stats.max_effective_width, width);

  const auto better = [&](std::size_t lhs, std::size_t rhs) {
    return expected_contribution_better(nodes[lhs], nodes[rhs]);
  };
  if (candidates.size() > width) {
    std::nth_element(candidates.begin(), candidates.begin()+width, candidates.end(), better);
    stats.pruned_nodes += candidates.size() - width;
    candidates.resize(width);
  }
  // order is a unique tie breaker, so this is identical to the old full
  // stable_sort followed by truncation, including next-layer generation order.
  std::sort(candidates.begin(), candidates.end(), better);
  stats.retained_nodes += candidates.size();

  // Remove pruned action-afterstates from their chance groups. A chance
  // outcome with no surviving action is backed up using its parent's
  // afterstate value, preserving the full probability mass without evaluating
  // the intermediate spawn state.
  std::vector<bool> retained(nodes.size(), false);
  for (const std::size_t candidate : candidates) retained[candidate] = true;
  for (const std::size_t parent_id : frontier) {
    const auto first = nodes[parent_id].first_chance;
    const auto end = first + nodes[parent_id].chance_count;
    for (std::size_t group_id = first; group_id < end; ++group_id) {
      auto& group = chance_groups[group_id];
      std::size_t kept = 0;
      for (std::size_t i = 0; i < group.child_count; ++i) {
        const auto child = group.afterstate_children[i];
        if (retained[child]) group.afterstate_children[kept++] = child;
      }
      group.child_count = kept;
    }
  }
  next_frontier = std::move(candidates);
  return !expired(deadline, stats);
}

double expectimax_backup(std::size_t node_id,
                         const std::vector<Node>& nodes,
                         const std::vector<ChanceGroup>& chance_groups) {
  const Node& node = nodes[node_id];
  if (!node.expanded || node.chance_count == 0) return node.value;

  double expected = 0.0;
  for (std::size_t group_id = node.first_chance;
       group_id < node.first_chance + node.chance_count; ++group_id) {
    const ChanceGroup& group = chance_groups[group_id];
    double decision_value = node.value;
    if (group.terminal) {
      decision_value = group.terminal_value;
    } else if (group.child_count != 0) {
      decision_value = -std::numeric_limits<double>::infinity();
      for (std::size_t i = 0; i < group.child_count; ++i) {
        const auto child = group.afterstate_children[i];
        decision_value = std::max(
            decision_value,
            expectimax_backup(child, nodes, chance_groups));
      }
    }
    expected += group.probability * decision_value;
  }
  return expected;
}

}  // namespace

Result search_afterstate(const board_t& board, const Config& config,
                         const Evaluator& evaluator) {
  if (config.depth < 1 || config.width < 1 || config.reward_scale <= 0 ||
      !evaluator || config.time_limit_ms == 0 ||
      config.time_limit_ms > static_cast<std::uint64_t>(
                                 std::numeric_limits<std::int64_t>::max())) {
    return search(board, config, evaluator);  // Reuse canonical validation.
  }
  (void)policy_width_limit(config, 1, {});

  Result result;
  result.frontier_exhausted = true;
  const Board root = make_board(board);
  const auto start = Clock::now();
  const auto available = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::time_point::max() - start);
  const Clock::time_point deadline =
      config.time_limit_ms >= static_cast<std::uint64_t>(available.count())
          ? Clock::time_point::max()
          : start + std::chrono::milliseconds(
                        static_cast<std::int64_t>(config.time_limit_ms));

  std::array<Board, 4> root_afterstates{};
  std::array<int, 4> root_rewards{};
  for (int direction = UP; direction <= LEFT; ++direction) {
    root_rewards[direction] = moveB(root.data(), root_afterstates[direction].data(),
                                    static_cast<move_dir>(direction));
    if (root_rewards[direction] >= 0) {
      result.legal[direction] = true;
      if (result.selected_move < 0) result.selected_move = direction;
    }
  }

  CachedEvaluator cached_evaluator(evaluator, result.stats, config.depth > 1);
  // Reuse capacity across the four root directions; no pointers survive clear.
  std::vector<Node> nodes;
  std::vector<ChanceGroup> chance_groups;
  std::vector<std::size_t> frontier, next_frontier;
  double best_root_value = -std::numeric_limits<double>::infinity();
  for (int direction = UP; direction <= LEFT; ++direction) {
    if (!result.legal[direction]) continue;
    if (expired(deadline, result.stats)) {
      result.status = SearchStatus::TimedOut;
      return result;
    }
    nodes.clear();
    nodes.emplace_back();
    Node& root_node = nodes.front();
    root_node.afterstate = root_afterstates[direction];
    root_node.probability = 1.0;
    root_node.accumulated_reward =
        static_cast<double>(root_rewards[direction]) * config.reward_scale;
    root_node.value = root_node.accumulated_reward +
                      cached_evaluator(root_node.afterstate);

    chance_groups.clear();
    frontier.assign(1, 0);
    result.max_depth_reached = std::max(result.max_depth_reached, 1);
    std::uint64_t order = 0;
    for (int depth = 1; depth < config.depth && !frontier.empty(); ++depth) {
      next_frontier.clear();
      if (!expand_afterstate_layer(frontier, nodes, chance_groups, config,
                                   cached_evaluator, static_cast<std::size_t>(depth),
                                   deadline, result.stats, order,
                                   next_frontier)) {
        result.status = SearchStatus::TimedOut;
        return result;
      }
      frontier.swap(next_frontier);
      if (!frontier.empty()) result.max_depth_reached =
          std::max(result.max_depth_reached, depth+1);
    }

    if (!frontier.empty()) result.frontier_exhausted = false;
    result.values[direction] = expectimax_backup(0, nodes, chance_groups);
    if (result.values[direction] > best_root_value) {
      best_root_value = result.values[direction];
      result.selected_move = direction;
    }
    if (expired(deadline, result.stats)) {
      result.status = SearchStatus::TimedOut;
      return result;
    }
  }
  return result;
}

}  // namespace beam2048
