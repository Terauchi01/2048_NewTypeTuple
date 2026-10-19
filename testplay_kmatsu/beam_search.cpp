#include "beam_search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace beam2048 {
namespace {

struct FrontierNode {
  Board board{};
  double probability = 0.0;
  double accumulated_reward = 0.0;
  double value = 0.0;
  bool terminal = false;
  std::uint64_t generation_order = 0;
};

struct BestResponse {
  bool has_move = false;
  Board board{};
  int reward = 0;
  double evaluation = 0.0;
};

using Clock = std::chrono::steady_clock;

bool deadline_reached(const Clock::time_point deadline, Stats& stats) {
  ++stats.deadline_checks;
  return Clock::now() >= deadline;
}

double checked_evaluation(const Evaluator& evaluator, const Board& board,
                          Stats& stats) {
  const double value = evaluator(board);
  ++stats.evaluated_nodes;
  if (!std::isfinite(value)) {
    throw std::domain_error("beam-search evaluator returned a non-finite value");
  }
  return value;
}

BestResponse best_response(const Board& board, const Config& config,
                           const Evaluator& evaluator,
                           const Clock::time_point deadline, Stats& stats,
                           bool& timed_out) {
  BestResponse best;
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

    const double evaluation = checked_evaluation(evaluator, next, stats);
    const double value =
        static_cast<double>(reward) * config.reward_scale + evaluation;
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

double normalized_expected_value(const std::vector<FrontierNode>& frontier) {
  double probability_sum = 0.0;
  double weighted_value_sum = 0.0;
  for (const FrontierNode& node : frontier) {
    probability_sum += node.probability;
    weighted_value_sum += node.probability * node.value;
  }
  if (probability_sum <= 0.0) {
    return -std::numeric_limits<double>::infinity();
  }
  return weighted_value_sum / probability_sum;
}

bool expand_layer(
    const std::vector<FrontierNode>& frontier, const Config& config,
    const Evaluator& evaluator, std::size_t layer_depth,
    const Clock::time_point deadline, Stats& stats,
    std::uint64_t& generation_order, std::vector<FrontierNode>& output) {
  std::vector<FrontierNode> candidates;

  for (const FrontierNode& parent : frontier) {
    if (deadline_reached(deadline, stats)) return false;
    ++stats.expanded_nodes;
    if (parent.terminal) {
      FrontierNode terminal = parent;
      terminal.generation_order = generation_order++;
      candidates.push_back(terminal);
      ++stats.generated_nodes;
      continue;
    }

    const std::vector<Spawn> spawns = generate_spawns(parent.board);
    if (spawns.empty()) {
      FrontierNode terminal = parent;
      terminal.terminal = true;
      terminal.value = terminal.accumulated_reward;
      terminal.generation_order = generation_order++;
      candidates.push_back(terminal);
      ++stats.generated_nodes;
      continue;
    }

    for (const Spawn& spawn : spawns) {
      if (deadline_reached(deadline, stats)) return false;
      FrontierNode child;
      child.probability = parent.probability * spawn.probability;
      child.accumulated_reward = parent.accumulated_reward;
      child.generation_order = generation_order++;

      bool timed_out = false;
      const BestResponse response = best_response(
          spawn.board, config, evaluator, deadline, stats, timed_out);
      if (timed_out) return false;
      if (response.has_move) {
        child.board = response.board;
        child.accumulated_reward +=
            static_cast<double>(response.reward) * config.reward_scale;
        child.value = child.accumulated_reward + response.evaluation;
      } else {
        child.board = spawn.board;
        child.value = child.accumulated_reward;
        child.terminal = true;
      }
      candidates.push_back(std::move(child));
      ++stats.generated_nodes;
    }
  }

  if (deadline_reached(deadline, stats)) return false;

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const FrontierNode& lhs, const FrontierNode& rhs) {
                     if (lhs.value != rhs.value) return lhs.value > rhs.value;
                     return lhs.generation_order < rhs.generation_order;
                   });
  if (deadline_reached(deadline, stats)) return false;
  std::vector<double> candidate_values;
  if (config.width_policy == WidthPolicy::Adaptive) {
    candidate_values.reserve(candidates.size());
    for (const FrontierNode& candidate : candidates) {
      candidate_values.push_back(candidate.value);
    }
  }
  const std::size_t effective_width =
      policy_width_limit(config, layer_depth, candidate_values);
  ++stats.beam_layers;
  stats.effective_width_sum += effective_width;
  if (stats.min_effective_width == 0 ||
      effective_width < stats.min_effective_width) {
    stats.min_effective_width = effective_width;
  }
  stats.max_effective_width =
      std::max(stats.max_effective_width, effective_width);

  if (candidates.size() > effective_width) {
    stats.pruned_nodes += candidates.size() - effective_width;
    candidates.resize(effective_width);
  }
  stats.retained_nodes += candidates.size();
  output = std::move(candidates);
  return true;
}

std::size_t capped_ceil(long double value, std::size_t cap) {
  if (!std::isfinite(value) || value >= static_cast<long double>(cap)) {
    return cap;
  }
  if (value <= 1.0L) return 1;
  const long double nearest = std::round(value);
  const long double tolerance =
      16.0L * std::numeric_limits<long double>::epsilon() * value;
  const long double rounded =
      std::fabs(value - nearest) <= tolerance ? nearest : std::ceil(value);
  if (rounded >= static_cast<long double>(cap)) return cap;
  return std::max<std::size_t>(1, static_cast<std::size_t>(rounded));
}

std::size_t capped_exponential(long double log_value, std::size_t cap) {
  if (cap == 1) return 1;
  const long double log_cap = std::log(static_cast<long double>(cap));
  if (log_value == -std::numeric_limits<long double>::infinity()) return 1;
  if (!std::isfinite(log_value) || log_value >= log_cap) return cap;
  return capped_ceil(std::exp(log_value), cap);
}

}  // namespace

Stats& Stats::operator+=(const Stats& other) {
  expanded_nodes += other.expanded_nodes;
  generated_nodes += other.generated_nodes;
  evaluated_nodes += other.evaluated_nodes;
  pruned_nodes += other.pruned_nodes;
  if (other.beam_layers > 0) {
    if (beam_layers == 0 || other.min_effective_width < min_effective_width) {
      min_effective_width = other.min_effective_width;
    }
    max_effective_width =
        std::max(max_effective_width, other.max_effective_width);
  }
  beam_layers += other.beam_layers;
  effective_width_sum += other.effective_width_sum;
  retained_nodes += other.retained_nodes;
  deadline_checks += other.deadline_checks;
  return *this;
}

Board make_board(const board_t& board) {
  Board result{};
  std::copy(std::begin(board), std::end(board), result.begin());
  return result;
}

std::vector<Spawn> generate_spawns(const Board& board) {
  std::size_t empty_count = 0;
  for (const int tile : board) {
    if (tile == 0) ++empty_count;
  }
  if (empty_count == 0) return {};

  std::vector<Spawn> spawns;
  spawns.reserve(empty_count * 2);
  const double probability_two = 0.9 / static_cast<double>(empty_count);
  const double probability_four = 0.1 / static_cast<double>(empty_count);
  for (std::size_t index = 0; index < board.size(); ++index) {
    if (board[index] != 0) continue;

    Spawn two;
    two.board = board;
    two.board[index] = 1;
    two.probability = probability_two;
    spawns.push_back(std::move(two));

    Spawn four;
    four.board = board;
    four.board[index] = 2;
    four.probability = probability_four;
    spawns.push_back(std::move(four));
  }
  return spawns;
}

std::size_t retention_limit(std::uint64_t evaluator_calls,
                            std::size_t hard_cap) {
  if (hard_cap == 0) {
    throw std::invalid_argument("beam-search hard width cap must be positive");
  }

  // sqrt(UINT64_MAX) is UINT32_MAX. Keep an exclusive upper bound of 2^32
  // and compare by division so mid * mid cannot overflow.
  std::uint64_t low = 0;
  std::uint64_t high = std::uint64_t{1} << 32;
  while (low + 1 < high) {
    const std::uint64_t middle = low + (high - low) / 2;
    if (middle <= evaluator_calls / middle) {
      low = middle;
    } else {
      high = middle;
    }
  }
  const std::uint64_t at_least_one = std::max<std::uint64_t>(1, low);
  if (at_least_one >= hard_cap) return hard_cap;
  return static_cast<std::size_t>(at_least_one);
}

std::string_view width_policy_name(WidthPolicy policy) {
  switch (policy) {
    case WidthPolicy::Fixed:
      return "fixed";
    case WidthPolicy::Sqrt:
      return "sqrt";
    case WidthPolicy::Cbrt:
      return "cbrt";
    case WidthPolicy::Power:
      return "power";
    case WidthPolicy::Increasing:
      return "increasing";
    case WidthPolicy::Decreasing:
      return "decreasing";
    case WidthPolicy::Adaptive:
      return "adaptive";
  }
  return "unknown";
}

std::size_t policy_width_limit(const Config& config, std::size_t layer_depth,
                               const std::vector<double>& candidate_values) {
  if (config.width == 0) {
    throw std::invalid_argument("beam-search maximum width must be positive");
  }
  if (layer_depth == 0) {
    throw std::invalid_argument("beam-search layer depth must be positive");
  }

  switch (config.width_policy) {
    case WidthPolicy::Fixed:
      if (config.fixed_width == 0) {
        throw std::invalid_argument("fixed beam width must be positive");
      }
      return std::min(config.width, config.fixed_width);
    case WidthPolicy::Sqrt:
      if (layer_depth >= std::numeric_limits<std::size_t>::digits) {
        return config.width;
      }
      return std::min(config.width, std::size_t{1} << layer_depth);
    case WidthPolicy::Cbrt:
      return capped_exponential(
          static_cast<long double>(layer_depth) * std::log(4.0L) / 3.0L,
          config.width);
    case WidthPolicy::Power:
      if (!std::isfinite(config.alpha) || config.alpha < 0.0) {
        throw std::invalid_argument("power alpha must be finite and non-negative");
      }
      return capped_exponential(
          static_cast<long double>(config.alpha) *
              static_cast<long double>(layer_depth) * std::log(4.0L),
          config.width);
    case WidthPolicy::Increasing:
    case WidthPolicy::Decreasing: {
      if (!std::isfinite(config.initial_width) || config.initial_width <= 0.0 ||
          !std::isfinite(config.width_ratio) || config.width_ratio <= 0.0) {
        throw std::invalid_argument("W0 and width ratio must be finite and positive");
      }
      const long double sign =
          config.width_policy == WidthPolicy::Increasing ? 1.0L : -1.0L;
      const long double log_value =
          std::log(static_cast<long double>(config.initial_width)) +
          sign * static_cast<long double>(layer_depth - 1) *
              std::log(static_cast<long double>(config.width_ratio));
      return capped_exponential(log_value, config.width);
    }
    case WidthPolicy::Adaptive: {
      if (!std::isfinite(config.adaptive_sigma) ||
          config.adaptive_sigma < 0.0) {
        throw std::invalid_argument("adaptive sigma must be finite and non-negative");
      }
      if (candidate_values.empty()) return 1;

      // Welford's method avoids catastrophic cancellation for large values.
      long double mean = 0.0L;
      long double sum_squared_deviations = 0.0L;
      std::size_t count = 0;
      double best = -std::numeric_limits<double>::infinity();
      for (const double value : candidate_values) {
        if (!std::isfinite(value)) {
          throw std::domain_error("adaptive beam received a non-finite value");
        }
        best = std::max(best, value);
        ++count;
        const long double delta = static_cast<long double>(value) - mean;
        mean += delta / static_cast<long double>(count);
        const long double delta_after = static_cast<long double>(value) - mean;
        sum_squared_deviations += delta * delta_after;
      }
      const long double standard_deviation =
          std::sqrt(std::max(0.0L, sum_squared_deviations /
                                      static_cast<long double>(count)));
      const long double threshold =
          static_cast<long double>(best) -
          static_cast<long double>(config.adaptive_sigma) * standard_deviation;
      std::size_t retained = 0;
      for (const double value : candidate_values) {
        if (static_cast<long double>(value) >= threshold) ++retained;
      }
      return std::min(config.width, std::max<std::size_t>(1, retained));
    }
  }
  throw std::invalid_argument("unknown beam width policy");
}

Result search(const board_t& board, const Config& config,
              const Evaluator& evaluator) {
  if (config.depth < 1) {
    throw std::invalid_argument("beam-search depth must be at least 1");
  }
  if (config.width < 1) {
    throw std::invalid_argument("beam-search width must be at least 1");
  }
  if (config.reward_scale <= 0) {
    throw std::invalid_argument("beam-search reward_scale must be positive");
  }
  if (!evaluator) {
    throw std::invalid_argument("beam-search evaluator must not be empty");
  }
  if (config.time_limit_ms == 0 ||
      config.time_limit_ms >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    throw std::invalid_argument("beam-search time limit must be positive");
  }
  // Validate policy-specific parameters even when depth == 1.
  (void)policy_width_limit(config, 1, {});

  Result result;
  const Board root = make_board(board);
  const Clock::time_point search_start = Clock::now();
  const Clock::duration available_budget = Clock::time_point::max() - search_start;
  const auto available_milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(available_budget);
  const Clock::time_point deadline =
      config.time_limit_ms >=
              static_cast<std::uint64_t>(available_milliseconds.count())
          ? Clock::time_point::max()
          : search_start + std::chrono::milliseconds(
                               static_cast<std::int64_t>(config.time_limit_ms));
  double best_value = -std::numeric_limits<double>::infinity();

  std::array<Board, 4> root_afterstates{};
  std::array<int, 4> root_rewards{};
  for (int direction = UP; direction <= LEFT; ++direction) {
    root_rewards[direction] =
        moveB(root.data(), root_afterstates[direction].data(),
              static_cast<move_dir>(direction));
    if (root_rewards[direction] < 0) continue;
    result.legal[direction] = true;
    if (result.selected_move < 0) result.selected_move = direction;
  }

  for (int direction = UP; direction <= LEFT; ++direction) {
    if (!result.legal[direction]) continue;
    if (deadline_reached(deadline, result.stats)) {
      result.status = SearchStatus::TimedOut;
      return result;
    }
    const Board& afterstate = root_afterstates[direction];
    const int reward = root_rewards[direction];
    const double root_reward =
        static_cast<double>(reward) * config.reward_scale;

    if (config.depth == 1) {
      result.values[direction] =
          root_reward + checked_evaluation(evaluator, afterstate, result.stats);
    } else {
      std::vector<FrontierNode> frontier(1);
      frontier.front().board = afterstate;
      frontier.front().probability = 1.0;
      frontier.front().accumulated_reward = root_reward;
      frontier.front().value = root_reward;

      std::uint64_t generation_order = 0;
      for (int ply = 1; ply < config.depth && !frontier.empty(); ++ply) {
        std::vector<FrontierNode> next_frontier;
        if (!expand_layer(frontier, config, evaluator,
                          static_cast<std::size_t>(ply), deadline, result.stats,
                          generation_order, next_frontier)) {
          result.status = SearchStatus::TimedOut;
          return result;
        }
        frontier = std::move(next_frontier);
      }
      result.values[direction] = normalized_expected_value(frontier);
    }

    // Strict comparison makes the direction enum order the deterministic
    // tie-break rule.
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
