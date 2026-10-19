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
  std::vector<std::size_t> afterstate_children;
};

struct Node {
  Board afterstate{};
  double probability = 0.0;
  double accumulated_reward = 0.0;
  // Heuristic value at this afterstate; used at the horizon and for beam
  // ranking. Internal nodes receive their expectimax value in backup().
  double value = 0.0;
  bool expanded = false;
  std::vector<std::size_t> chance_children;
  std::uint64_t order = 0;
};

using Clock = std::chrono::steady_clock;

bool expired(Clock::time_point deadline, Stats& stats) {
  ++stats.deadline_checks;
  return Clock::now() >= deadline;
}

double evaluate(const Evaluator& evaluator, const Board& afterstate,
                Stats& stats) {
  const double value = evaluator(afterstate);
  ++stats.evaluated_nodes;
  if (!std::isfinite(value)) {
    throw std::domain_error("beam-search evaluator returned a non-finite value");
  }
  return value;
}

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
                             const Config& config, const Evaluator& evaluator,
                             std::size_t layer_depth,
                             Clock::time_point deadline, Stats& stats,
                             std::uint64_t& order,
                             std::vector<std::size_t>& next_frontier) {
  std::vector<std::size_t> candidates;
  candidates.clear();

  for (const std::size_t parent_id : frontier) {
    if (expired(deadline, stats)) return false;
    ++stats.expanded_nodes;
    nodes[parent_id].expanded = true;
    nodes[parent_id].chance_children.clear();
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
      nodes[parent_id].chance_children.push_back(group_id);

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
                      evaluate(evaluator, afterstate, stats);
        child.order = order++;
        const std::size_t child_id = nodes.size();
        nodes.push_back(std::move(child));
        chance_groups[group_id].afterstate_children.push_back(child_id);
        candidates.push_back(child_id);
        ++stats.generated_nodes;
      }

      // A spawn that leaves no legal move has a terminal value but is not an
      // afterstate, so it is never passed to the evaluator or beam selector.
      if (chance_groups[group_id].afterstate_children.empty()) {
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

  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](std::size_t lhs, std::size_t rhs) {
                     return expected_contribution_better(nodes[lhs], nodes[rhs]);
                   });
  if (candidates.size() > width) {
    stats.pruned_nodes += candidates.size() - width;
    candidates.resize(width);
  }
  stats.retained_nodes += candidates.size();

  // Remove pruned action-afterstates from their chance groups. A chance
  // outcome with no surviving action is backed up using its parent's
  // afterstate value, preserving the full probability mass without evaluating
  // the intermediate spawn state.
  std::vector<bool> retained(nodes.size(), false);
  for (const std::size_t candidate : candidates) retained[candidate] = true;
  for (const std::size_t parent_id : frontier) {
    for (const std::size_t group_id : nodes[parent_id].chance_children) {
      auto& children = chance_groups[group_id].afterstate_children;
      children.erase(std::remove_if(children.begin(), children.end(),
                                    [&](std::size_t child) {
                                      return !retained[child];
                                    }),
                     children.end());
    }
  }
  next_frontier = std::move(candidates);
  return !expired(deadline, stats);
}

double expectimax_backup(std::size_t node_id,
                         const std::vector<Node>& nodes,
                         const std::vector<ChanceGroup>& chance_groups) {
  const Node& node = nodes[node_id];
  if (!node.expanded || node.chance_children.empty()) return node.value;

  double expected = 0.0;
  for (const std::size_t group_id : node.chance_children) {
    const ChanceGroup& group = chance_groups[group_id];
    double decision_value = node.value;
    if (group.terminal) {
      decision_value = group.terminal_value;
    } else if (!group.afterstate_children.empty()) {
      decision_value = -std::numeric_limits<double>::infinity();
      for (const std::size_t child : group.afterstate_children) {
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

  double best_root_value = -std::numeric_limits<double>::infinity();
  for (int direction = UP; direction <= LEFT; ++direction) {
    if (!result.legal[direction]) continue;
    if (expired(deadline, result.stats)) {
      result.status = SearchStatus::TimedOut;
      return result;
    }
    std::vector<Node> nodes(1);
    Node& root_node = nodes.front();
    root_node.afterstate = root_afterstates[direction];
    root_node.probability = 1.0;
    root_node.accumulated_reward =
        static_cast<double>(root_rewards[direction]) * config.reward_scale;
    root_node.value = root_node.accumulated_reward +
                      evaluate(evaluator, root_node.afterstate, result.stats);

    std::vector<ChanceGroup> chance_groups;
    std::vector<std::size_t> frontier{0};
    std::uint64_t order = 0;
    for (int depth = 1; depth < config.depth && !frontier.empty(); ++depth) {
      std::vector<std::size_t> next_frontier;
      if (!expand_afterstate_layer(frontier, nodes, chance_groups, config,
                                   evaluator, static_cast<std::size_t>(depth),
                                   deadline, result.stats, order,
                                   next_frontier)) {
        result.status = SearchStatus::TimedOut;
        return result;
      }
      frontier.swap(next_frontier);
    }

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
