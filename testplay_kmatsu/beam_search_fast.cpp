#include "beam_search_fast.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace beam2048 {
namespace {

struct FastNode {
  Board board{};
  double probability = 0.0;
  double accumulated_reward = 0.0;
  double value = 0.0;
  bool terminal = false;
  std::uint64_t generation_order = 0;
};

struct FastResponse {
  bool has_move = false;
  Board board{};
  int reward = 0;
  double evaluation = 0.0;
};

using Clock = std::chrono::steady_clock;

bool deadline_reached(Clock::time_point deadline, Stats& stats) {
  ++stats.deadline_checks;
  return Clock::now() >= deadline;
}

double evaluate_checked(const Evaluator& evaluator, const Board& board,
                        Stats& stats) {
  const double value = evaluator(board);
  ++stats.evaluated_nodes;
  if (!std::isfinite(value)) {
    throw std::domain_error("beam-search evaluator returned a non-finite value");
  }
  return value;
}

FastResponse best_response(const Board& board, const Config& config,
                           const Evaluator& evaluator,
                           Clock::time_point deadline, Stats& stats,
                           bool& timed_out) {
  FastResponse best;
  double best_value = -std::numeric_limits<double>::infinity();
  for (int direction = UP; direction <= LEFT; ++direction) {
    if (deadline_reached(deadline, stats)) {
      timed_out = true;
      return best;
    }
    Board next{};
    const int reward =
        moveB(board.data(), next.data(), static_cast<move_dir>(direction));
    if (reward < 0) continue;
    const double evaluation = evaluate_checked(evaluator, next, stats);
    const double value = static_cast<double>(reward) * config.reward_scale +
                         evaluation;
    if (!best.has_move || value > best_value) {
      best.has_move = true;
      best.board = next;
      best.reward = reward;
      best.evaluation = evaluation;
      best_value = value;
    }
    if (deadline_reached(deadline, stats)) {
      timed_out = true;
      return best;
    }
  }
  return best;
}

bool node_better(const FastNode& lhs, const FastNode& rhs) {
  if (lhs.value != rhs.value) return lhs.value > rhs.value;
  return lhs.generation_order < rhs.generation_order;
}

double expected_value(const std::vector<FastNode>& frontier) {
  double probability_sum = 0.0;
  double weighted_value_sum = 0.0;
  for (const FastNode& node : frontier) {
    probability_sum += node.probability;
    weighted_value_sum += node.probability * node.value;
  }
  if (probability_sum <= 0.0) {
    return -std::numeric_limits<double>::infinity();
  }
  return weighted_value_sum / probability_sum;
}

// Spawns are enumerated in exactly the same index, exponent(2,4) order as
// generate_spawns(), while avoiding one temporary vector allocation per node.
bool expand_layer_fast(const std::vector<FastNode>& frontier,
                       const Config& config, const Evaluator& evaluator,
                       std::size_t layer_depth, Clock::time_point deadline,
                       Stats& stats, std::uint64_t& generation_order,
                       std::vector<FastNode>& candidates,
                       std::vector<double>& candidate_values) {
  candidates.clear();
  candidate_values.clear();
  for (const FastNode& parent : frontier) {
    if (deadline_reached(deadline, stats)) return false;
    ++stats.expanded_nodes;
    if (parent.terminal) {
      FastNode terminal = parent;
      terminal.generation_order = generation_order++;
      candidates.push_back(terminal);
      ++stats.generated_nodes;
      continue;
    }

    std::size_t empty_count = 0;
    for (int tile : parent.board) empty_count += tile == 0;
    if (empty_count == 0) {
      FastNode terminal = parent;
      terminal.terminal = true;
      terminal.value = terminal.accumulated_reward;
      terminal.generation_order = generation_order++;
      candidates.push_back(terminal);
      ++stats.generated_nodes;
      continue;
    }

    const double probability_two = 0.9 / static_cast<double>(empty_count);
    const double probability_four = 0.1 / static_cast<double>(empty_count);
    for (std::size_t cell = 0; cell < parent.board.size(); ++cell) {
      if (parent.board[cell] != 0) continue;
      for (int exponent = 1; exponent <= 2; ++exponent) {
        if (deadline_reached(deadline, stats)) return false;
        Board spawned = parent.board;
        spawned[cell] = exponent;
        FastNode child;
        child.probability = parent.probability *
                            (exponent == 1 ? probability_two : probability_four);
        child.accumulated_reward = parent.accumulated_reward;
        child.generation_order = generation_order++;

        bool timed_out = false;
        const FastResponse response = best_response(
            spawned, config, evaluator, deadline, stats, timed_out);
        if (timed_out) return false;
        if (response.has_move) {
          child.board = response.board;
          child.accumulated_reward +=
              static_cast<double>(response.reward) * config.reward_scale;
          child.value = child.accumulated_reward + response.evaluation;
        } else {
          child.board = spawned;
          child.value = child.accumulated_reward;
          child.terminal = true;
        }
        candidates.push_back(std::move(child));
        ++stats.generated_nodes;
      }
    }
  }

  if (deadline_reached(deadline, stats)) return false;
  if (config.width_policy == WidthPolicy::Adaptive) {
    candidate_values.reserve(candidates.size());
    for (const FastNode& candidate : candidates) {
      candidate_values.push_back(candidate.value);
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

  std::size_t pruned = 0;
  if (candidates.size() > width) {
    // Select exactly the same deterministic top-W set as stable_sort, then
    // order only that retained prefix for identical downstream traversal.
    pruned = candidates.size() - width;
    std::nth_element(candidates.begin(), candidates.begin() + width,
                     candidates.end(), node_better);
    candidates.resize(width);
    std::sort(candidates.begin(), candidates.end(), node_better);
  } else {
    std::sort(candidates.begin(), candidates.end(), node_better);
  }
  if (deadline_reached(deadline, stats)) return false;
  stats.pruned_nodes += pruned;
  stats.retained_nodes += candidates.size();
  return true;
}

}  // namespace

Result search_fast(const board_t& board, const Config& config,
                   const Evaluator& evaluator) {
  // Reuse the baseline validation and policy parameter checks.
  if (config.depth < 1 || config.width < 1 || config.reward_scale <= 0 ||
      !evaluator || config.time_limit_ms == 0 ||
      config.time_limit_ms > static_cast<std::uint64_t>(
                                 std::numeric_limits<std::int64_t>::max())) {
    return search(board, config, evaluator);
  }
  (void)policy_width_limit(config, 1, {});

  Result result;
  const Board root = make_board(board);
  const Clock::time_point started = Clock::now();
  const auto available = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::time_point::max() - started);
  const Clock::time_point deadline =
      config.time_limit_ms >= static_cast<std::uint64_t>(available.count())
          ? Clock::time_point::max()
          : started + std::chrono::milliseconds(
                          static_cast<std::int64_t>(config.time_limit_ms));
  double best_value = -std::numeric_limits<double>::infinity();
  std::array<Board, 4> afterstates{};
  std::array<int, 4> rewards{};
  for (int direction = UP; direction <= LEFT; ++direction) {
    rewards[direction] = moveB(root.data(), afterstates[direction].data(),
                               static_cast<move_dir>(direction));
    if (rewards[direction] < 0) continue;
    result.legal[direction] = true;
    if (result.selected_move < 0) result.selected_move = direction;
  }

  for (int direction = UP; direction <= LEFT; ++direction) {
    if (!result.legal[direction]) continue;
    if (deadline_reached(deadline, result.stats)) {
      result.status = SearchStatus::TimedOut;
      return result;
    }
    const double root_reward =
        static_cast<double>(rewards[direction]) * config.reward_scale;
    if (config.depth == 1) {
      result.values[direction] =
          root_reward + evaluate_checked(evaluator, afterstates[direction],
                                         result.stats);
    } else {
      std::vector<FastNode> frontier(1);
      frontier.front().board = afterstates[direction];
      frontier.front().probability = 1.0;
      frontier.front().accumulated_reward = root_reward;
      frontier.front().value = root_reward;
      std::vector<FastNode> next_frontier;
      std::vector<double> candidate_values;
      std::uint64_t generation_order = 0;
      for (int ply = 1; ply < config.depth && !frontier.empty(); ++ply) {
        if (!expand_layer_fast(frontier, config, evaluator,
                               static_cast<std::size_t>(ply), deadline,
                               result.stats, generation_order, next_frontier,
                               candidate_values)) {
          result.status = SearchStatus::TimedOut;
          return result;
        }
        frontier.swap(next_frontier);
      }
      result.values[direction] = expected_value(frontier);
    }
    if (result.selected_move < 0 || result.values[direction] > best_value) {
      result.selected_move = direction;
      best_value = result.values[direction];
    }
    if (deadline_reached(deadline, result.stats)) {
      result.status = SearchStatus::TimedOut;
      return result;
    }
  }
  return result;
}

}  // namespace beam2048
