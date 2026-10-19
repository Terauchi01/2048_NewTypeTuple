#include "beam_search_afterstate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                   #condition);                                                \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

double deterministic_evaluation(const beam2048::Board& board) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t index = 0; index < board.size(); ++index) {
    hash ^= static_cast<std::uint64_t>(board[index] + 17 * index);
    hash *= 1099511628211ULL;
  }
  return static_cast<double>(hash % 200001ULL) - 100000.0;
}

double exact_expectimax(const beam2048::Board& afterstate, int depth,
                        double accumulated_reward) {
  if (depth == 1) return accumulated_reward + deterministic_evaluation(afterstate);

  double expected = 0.0;
  for (const beam2048::Spawn& spawn : beam2048::generate_spawns(afterstate)) {
    double best = -std::numeric_limits<double>::infinity();
    for (int direction = UP; direction <= LEFT; ++direction) {
      beam2048::Board next{};
      const int reward = moveB(spawn.board.data(), next.data(),
                               static_cast<move_dir>(direction));
      if (reward < 0) continue;
      best = std::max(best, exact_expectimax(
                                next, depth - 1,
                                accumulated_reward + reward));
    }
    if (!std::isfinite(best)) best = accumulated_reward;
    expected += spawn.probability * best;
  }
  return expected;
}

void test_depth_one_is_one_afterstate_evaluation_per_legal_move() {
  board_t board{};
  board[0] = 1;
  board[15] = 1;
  beam2048::Config config;
  config.depth = 1;
  std::vector<beam2048::Board> evaluated;
  const beam2048::Result result = beam2048::search_afterstate(
      board, config, [&](const beam2048::Board& afterstate) {
        evaluated.push_back(afterstate);
        return 0.0;
      });
  CHECK(result.status == beam2048::SearchStatus::Completed);
  CHECK(result.stats.evaluated_nodes == evaluated.size());
  CHECK(evaluated.size() == 4);
  CHECK(result.selected_move >= UP && result.selected_move <= LEFT);
}

void test_depth_two_expands_chance_then_evaluates_afterstates() {
  board_t board{};
  board[0] = 1;
  board[15] = 1;
  beam2048::Config config;
  config.depth = 2;
  config.width = 1;
  config.fixed_width = 1;
  std::vector<beam2048::Board> evaluated;
  const beam2048::Result result = beam2048::search_afterstate(
      board, config, [&](const beam2048::Board& afterstate) {
        evaluated.push_back(afterstate);
        return 0.0;
      });
  CHECK(result.status == beam2048::SearchStatus::Completed);
  CHECK(result.stats.beam_layers == 4);
  CHECK(result.stats.pruned_nodes > 0);
  CHECK(result.stats.generated_nodes > result.stats.pruned_nodes);
  CHECK(result.stats.evaluated_nodes == evaluated.size());
  CHECK(evaluated.size() > 4);
  // Merges preserve the tile-value sum; the two original 2-tiles plus the
  // spawned tile therefore leave at least six tile-value units on board.
  for (const beam2048::Board& afterstate : evaluated) {
    int tile_sum = 0;
    for (const int exponent : afterstate) {
      if (exponent > 0) tile_sum += 1 << exponent;
    }
    CHECK(tile_sum >= 4);
  }
}

void test_unpruned_depth_two_and_three_match_expectimax() {
  board_t board{};
  board[0] = 1;
  board[15] = 1;
  for (const int depth : {2, 3}) {
    beam2048::Config config;
    config.depth = depth;
    config.width = 100000;
    config.fixed_width = 100000;
    config.time_limit_ms = 60000;
    const beam2048::Result actual = beam2048::search_afterstate(
        board, config, deterministic_evaluation);
    CHECK(actual.status == beam2048::SearchStatus::Completed);
    CHECK(actual.stats.pruned_nodes == 0);

    const beam2048::Board root = beam2048::make_board(board);
    for (int direction = UP; direction <= LEFT; ++direction) {
      beam2048::Board afterstate{};
      const int reward = moveB(root.data(), afterstate.data(),
                               static_cast<move_dir>(direction));
      if (reward < 0) {
        CHECK(!actual.legal[direction]);
        continue;
      }
      const double expected = exact_expectimax(afterstate, depth, reward);
      CHECK(std::fabs(actual.values[direction] - expected) <= 1e-8);
    }
  }
}

}  // namespace

int main() {
  init_movetable();
  test_depth_one_is_one_afterstate_evaluation_per_legal_move();
  test_depth_two_expands_chance_then_evaluates_afterstates();
  test_unpruned_depth_two_and_three_match_expectimax();
  if (failures != 0) {
    std::fprintf(stderr, "%d afterstate beam test(s) failed\n", failures);
    return 1;
  }
  std::puts("afterstate beam tests passed");
  return 0;
}
