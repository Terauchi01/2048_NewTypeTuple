#ifndef BEAM_SEARCH_H
#define BEAM_SEARCH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <vector>

#include "../head/game2048.h"

namespace beam2048 {

using Board = std::array<int, 16>;
using Evaluator = std::function<double(const Board&)>;

enum class WidthPolicy {
  Fixed,
  Sqrt,
  SqrtBudget,
  Cbrt,
  Power,
  Increasing,
  Decreasing,
  Adaptive,
};

enum class SearchStatus {
  Completed,
  TimedOut,
};

struct Config {
  // Number of player moves in the search, including the root move.
  int depth = 1;
  // Maximum number of chance branches retained per root move and layer.
  std::size_t width = 1;
  // Converts the integer merge reward to the evaluator's value scale. The
  // Beam runner uses a native double evaluator, so its default scale is one.
  int reward_scale = 1;
  WidthPolicy width_policy = WidthPolicy::Fixed;
  std::size_t fixed_width = 1;
  // Width-control budget for SqrtBudget; depth/time limits stop the search.
  std::uint64_t width_budget = 100000;
  double alpha = 0.5;
  double initial_width = 1.0;
  double width_ratio = 2.0;
  double adaptive_sigma = 1.0;
  // A single search is interrupted when this steady-clock budget expires.
  std::uint64_t time_limit_ms = 60000;
};

struct Stats {
  // Frontier nodes visited while producing the next search layer.
  std::uint64_t expanded_nodes = 0;
  // Chance outcomes turned into candidates (terminal outcomes included).
  std::uint64_t generated_nodes = 0;
  // Calls made to the supplied evaluator.
  std::uint64_t evaluated_nodes = 0;
  // Generated candidates discarded by the beam-width limit.
  std::uint64_t pruned_nodes = 0;
  // Number of root/layer beam-limit applications.
  std::uint64_t beam_layers = 0;
  // Sum of effective width limits, useful for computing the average limit.
  std::uint64_t effective_width_sum = 0;
  // Zero means that no beam layer was expanded (for example, depth == 1).
  std::size_t min_effective_width = 0;
  std::size_t max_effective_width = 0;
  // Candidates retained after applying the effective limit, summed by layer.
  std::uint64_t retained_nodes = 0;
  // Number of steady-clock deadline checks made by the search.
  std::uint64_t deadline_checks = 0;

  Stats& operator+=(const Stats& other);
};

struct Spawn {
  Board board{};
  double probability = 0.0;
};

struct Result {
  // -1 means that the root board has no legal move.
  int selected_move = -1;
  std::array<double, 4> values{
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity()};
  std::array<bool, 4> legal{false, false, false, false};
  SearchStatus status = SearchStatus::Completed;
  Stats stats{};
  int max_depth_reached = 0;
  bool frontier_exhausted = false;
};

Board make_board(const board_t& board);

// Generates every random-tile outcome in deterministic order: cell index
// ascending, then a 2-tile followed by a 4-tile. The probabilities over all
// outcomes sum to one. A full board has no outcomes.
std::vector<Spawn> generate_spawns(const Board& board);

// Returns min(hard_cap, max(1, floor(sqrt(evaluator_calls)))). The square
// root is computed exactly for the full uint64_t range. Throws
// std::invalid_argument when hard_cap is zero.
std::size_t retention_limit(std::uint64_t evaluator_calls,
                            std::size_t hard_cap);

// Returns the effective width for one completed layer. layer_depth == 1 is
// the first expansion after the root move. candidate_values is used only by
// Adaptive, which retains values >= best - sigma * population_stddev.
std::size_t policy_width_limit(const Config& config, std::size_t layer_depth,
                               const std::vector<double>& candidate_values);

std::string_view width_policy_name(WidthPolicy policy);

// Beam-search policy for stochastic 2048.
//
// A separate probability-carrying frontier is maintained for every legal
// root move. At each later player ply, all 2/4 spawn outcomes are generated;
// for each outcome only its highest-valued legal response is kept. Candidates
// are ranked by cumulative scaled merge reward plus evaluator(afterstate), and
// the configured width policy is computed independently for every root
// direction and layer. Config::width is always a hard upper bound.
// Equal values keep generation order, ultimately preferring directions
// UP, RIGHT, DOWN, LEFT. A root value is the probability-normalized expected
// value over its retained frontier. Config::depth == 1 is therefore exactly a
// one-ply greedy search.
//
// Throws std::invalid_argument for an invalid configuration or empty
// evaluator. A deadline expiry is reported as SearchStatus::TimedOut and the
// selected_move is a deterministic legal fallback (callers may choose not to
// apply it).
Result search(const board_t& board, const Config& config,
              const Evaluator& evaluator);

}  // namespace beam2048

#endif  // BEAM_SEARCH_H
