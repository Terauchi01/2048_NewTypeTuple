#include "beam_search.h"
#include "double_evaluator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
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

bool close_to(double actual, double expected, double tolerance = 1e-9) {
  return std::fabs(actual - expected) <= tolerance;
}

void copy_to_native(const beam2048::Board& source, board_t& destination) {
  for (std::size_t i = 0; i < source.size(); ++i) {
    destination[i] = source[i];
  }
}

beam2048::Board move_board(const beam2048::Board& board, int direction,
                           int& reward) {
  beam2048::Board next{};
  reward = moveB(board.data(), next.data(), static_cast<move_dir>(direction));
  return next;
}

std::array<double, 4> depth_two_oracle(
    const beam2048::Board& root, int reward_scale,
    const beam2048::Evaluator& evaluator) {
  std::array<double, 4> values{
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity()};

  for (int direction = UP; direction <= LEFT; ++direction) {
    int root_reward = 0;
    const beam2048::Board afterstate =
        move_board(root, direction, root_reward);
    if (root_reward < 0) continue;

    double expected_response = 0.0;
    const std::vector<beam2048::Spawn> spawns =
        beam2048::generate_spawns(afterstate);
    for (const beam2048::Spawn& spawn : spawns) {
      double best_response = -std::numeric_limits<double>::infinity();
      for (int response_direction = UP; response_direction <= LEFT;
           ++response_direction) {
        int response_reward = 0;
        const beam2048::Board response =
            move_board(spawn.board, response_direction, response_reward);
        if (response_reward < 0) continue;
        const double candidate =
            static_cast<double>(response_reward) * reward_scale +
            evaluator(response);
        if (candidate > best_response) best_response = candidate;
      }

      // A terminal spawn remains an outcome with value zero beyond the
      // reward already earned by the root move.
      if (!std::isfinite(best_response)) best_response = 0.0;
      expected_response += spawn.probability * best_response;
    }
    values[direction] =
        static_cast<double>(root_reward) * reward_scale + expected_response;
  }
  return values;
}

std::array<double, 4> depth_two_width_one_oracle(
    const beam2048::Board& root, int reward_scale,
    const beam2048::Evaluator& evaluator) {
  std::array<double, 4> values{
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity()};

  for (int direction = UP; direction <= LEFT; ++direction) {
    int root_reward = 0;
    const beam2048::Board afterstate =
        move_board(root, direction, root_reward);
    if (root_reward < 0) continue;

    double retained_value = -std::numeric_limits<double>::infinity();
    for (const beam2048::Spawn& spawn :
         beam2048::generate_spawns(afterstate)) {
      double best_response = -std::numeric_limits<double>::infinity();
      for (int response_direction = UP; response_direction <= LEFT;
           ++response_direction) {
        int response_reward = 0;
        const beam2048::Board response =
            move_board(spawn.board, response_direction, response_reward);
        if (response_reward < 0) continue;
        const double candidate =
            static_cast<double>(response_reward) * reward_scale +
            evaluator(response);
        if (candidate > best_response) best_response = candidate;
      }
      if (!std::isfinite(best_response)) best_response = 0.0;
      const double candidate =
          static_cast<double>(root_reward) * reward_scale + best_response;
      if (candidate > retained_value) retained_value = candidate;
    }
    values[direction] = retained_value;
  }
  return values;
}

void test_spawn_generation() {
  beam2048::Board board{
      1, 2, 0, 4,
      2, 3, 4, 1,
      3, 4, 1, 2,
      4, 1, 0, 3,
  };
  const std::vector<beam2048::Spawn> spawns =
      beam2048::generate_spawns(board);

  CHECK(spawns.size() == 4);
  CHECK(spawns[0].board[2] == 1);
  CHECK(spawns[1].board[2] == 2);
  CHECK(spawns[2].board[14] == 1);
  CHECK(spawns[3].board[14] == 2);
  CHECK(close_to(spawns[0].probability, 0.45));
  CHECK(close_to(spawns[1].probability, 0.05));
  CHECK(close_to(spawns[2].probability, 0.45));
  CHECK(close_to(spawns[3].probability, 0.05));

  double probability_sum = 0.0;
  for (const beam2048::Spawn& spawn : spawns) {
    probability_sum += spawn.probability;
    int difference_count = 0;
    for (std::size_t i = 0; i < board.size(); ++i) {
      if (spawn.board[i] != board[i]) ++difference_count;
    }
    CHECK(difference_count == 1);
  }
  CHECK(close_to(probability_sum, 1.0));

  board[2] = 1;
  board[14] = 2;
  CHECK(beam2048::generate_spawns(board).empty());
}

std::string shell_quote_for_test(const std::string& text) {
  std::string result("'");
  for (const char character : text) {
    if (character == '\'') {
      result += "'\\''";
    } else {
      result.push_back(character);
    }
  }
  result.push_back('\'');
  return result;
}

bool write_bytes(const std::filesystem::path& path, const void* data,
                 std::size_t size) {
  FILE* file = std::fopen(path.string().c_str(), "wb");
  if (file == nullptr) return false;
  const bool written = size == 0 || std::fwrite(data, 1, size, file) == size;
  return std::fclose(file) == 0 && written;
}

void test_double_loader_small_files() {
  namespace fs = std::filesystem;
  const fs::path directory =
      fs::temp_directory_path() /
      ("beam_double_loader_" + std::to_string(
                                   reinterpret_cast<std::uintptr_t>(&failures)));
  std::error_code filesystem_error;
  fs::create_directories(directory, filesystem_error);
  CHECK(!filesystem_error);

  const double values[] = {1.25, -2.5, 3.75, 9.0};
  const fs::path exact_path = directory / "exact.dat";
  const fs::path short_path = directory / "int_style_wrong_size.dat";
  const fs::path trailing_path = directory / "seven_tuple_style_too_large.dat";
  const fs::path empty_path = directory / "empty.dat";
  const fs::path xz_path = directory / "exact.dat.xz";
  CHECK(write_bytes(exact_path, values, 3 * sizeof(double)));
  CHECK(write_bytes(short_path, values, 2 * sizeof(double)));
  CHECK(write_bytes(trailing_path, values, 4 * sizeof(double)));
  CHECK(write_bytes(empty_path, nullptr, 0));

  beam2048::DoubleBuffer buffer;
  std::string error;
  CHECK(beam2048::load_exact_doubles(exact_path.string(), 3, buffer,
                                     &error));
  CHECK(error.empty());
  CHECK(buffer.size == 3);
  CHECK(close_to(buffer.data()[0], values[0]));
  CHECK(close_to(buffer.data()[1], values[1]));
  CHECK(close_to(buffer.data()[2], values[2]));

  // A failed reload must not leave the previously loaded table accessible.
  CHECK(!beam2048::load_exact_doubles(short_path.string(), 3, buffer,
                                      &error));
  CHECK(buffer.empty());
  CHECK(!error.empty());
  CHECK(!beam2048::load_exact_doubles(trailing_path.string(), 3, buffer,
                                      &error));
  CHECK(buffer.empty());
  CHECK(!error.empty());
  CHECK(!beam2048::load_exact_doubles("", 3, buffer, &error));
  CHECK(buffer.empty());
  CHECK(!error.empty());

  CHECK(beam2048::load_exact_doubles(empty_path.string(), 0, buffer,
                                     &error));
  CHECK(buffer.empty());
  CHECK(error.empty());

  const std::string compress_command =
      "xz -c -- " + shell_quote_for_test(exact_path.string()) + " > " +
      shell_quote_for_test(xz_path.string());
  CHECK(std::system(compress_command.c_str()) == 0);
  CHECK(beam2048::load_exact_doubles(xz_path.string(), 3, buffer, &error));
  CHECK(error.empty());
  CHECK(buffer.size == 3);
  CHECK(close_to(buffer.data()[0], values[0]));
  CHECK(close_to(buffer.data()[1], values[1]));
  CHECK(close_to(buffer.data()[2], values[2]));

  fs::remove_all(directory, filesystem_error);
  CHECK(!filesystem_error);
}

void test_double_evaluator_contract() {
  CHECK(beam2048::DoubleTupleEvaluator6::kValueCount == 612220032ULL);
  CHECK(beam2048::DoubleTupleEvaluator6::kByteCount == 4897760256ULL);

  beam2048::DoubleTupleEvaluator6 evaluator;
  CHECK(!evaluator.loaded());
  bool threw = false;
  try {
    evaluator.evaluate(beam2048::Board{});
  } catch (const std::logic_error&) {
    threw = true;
  } catch (...) {
  }
  CHECK(threw);
}

void test_depth_one_matches_greedy() {
  beam2048::Board board{};
  board[5] = 1;
  board_t native{};
  copy_to_native(board, native);

  const beam2048::Evaluator evaluator = [](const beam2048::Board& value) {
    double result = 0.0;
    for (std::size_t i = 0; i < value.size(); ++i) {
      result += static_cast<double>((i + 1) * value[i]);
    }
    return result;
  };
  const beam2048::Config config{1, 1, 1024};
  const beam2048::Result result = beam2048::search(native, config, evaluator);

  int expected_move = -1;
  double expected_best = -std::numeric_limits<double>::infinity();
  std::uint64_t legal_count = 0;
  for (int direction = UP; direction <= LEFT; ++direction) {
    int reward = 0;
    const beam2048::Board afterstate = move_board(board, direction, reward);
    if (reward < 0) {
      CHECK(!result.legal[direction]);
      CHECK(std::isinf(result.values[direction]));
      continue;
    }
    ++legal_count;
    const double expected =
        static_cast<double>(reward) * config.reward_scale +
        evaluator(afterstate);
    CHECK(result.legal[direction]);
    CHECK(close_to(result.values[direction], expected));
    if (expected_move < 0 || expected > expected_best) {
      expected_move = direction;
      expected_best = expected;
    }
  }
  CHECK(result.selected_move == expected_move);
  CHECK(result.stats.evaluated_nodes == legal_count);
  CHECK(result.stats.generated_nodes == 0);
  CHECK(result.stats.pruned_nodes == 0);
  CHECK(result.stats.beam_layers == 0);
  CHECK(result.stats.effective_width_sum == 0);
  CHECK(result.stats.min_effective_width == 0);
  CHECK(result.stats.max_effective_width == 0);
  CHECK(result.stats.retained_nodes == 0);
}

void test_q10_immediate_reward() {
  beam2048::Board board{};
  board[0] = 1;
  board[1] = 1;
  board_t native{};
  copy_to_native(board, native);

  const beam2048::Result result = beam2048::search(
      native, beam2048::Config{1, 1, 1024},
      [](const beam2048::Board&) { return 0.0; });

  CHECK(result.selected_move == RIGHT);
  CHECK(close_to(result.values[RIGHT], 4.0 * 1024.0));
  CHECK(close_to(result.values[LEFT], 4.0 * 1024.0));
  CHECK(close_to(result.values[DOWN], 0.0));
  CHECK(!result.legal[UP]);
}

void test_unscaled_double_immediate_reward() {
  beam2048::Board board{};
  board[0] = 1;
  board[1] = 1;
  board_t native{};
  copy_to_native(board, native);

  const beam2048::Result result = beam2048::search(
      native, beam2048::Config{1, 1, 1},
      [](const beam2048::Board&) { return 0.25; });

  // The learn_double evaluator is already expressed in score units. A 2+2
  // merge must therefore add 4 directly, without the legacy Q10 multiplier.
  CHECK(result.selected_move == RIGHT);
  CHECK(close_to(result.values[RIGHT], 4.25));
  CHECK(close_to(result.values[LEFT], 4.25));
  CHECK(close_to(result.values[DOWN], 0.25));
}

void test_deterministic_tie_break() {
  beam2048::Board board{};
  board[5] = 1;
  board_t native{};
  copy_to_native(board, native);

  const beam2048::Result first = beam2048::search(
      native, beam2048::Config{1, 1, 1024},
      [](const beam2048::Board&) { return 0.0; });
  const beam2048::Result second = beam2048::search(
      native, beam2048::Config{1, 1, 1024},
      [](const beam2048::Board&) { return 0.0; });

  CHECK(first.legal[UP] && first.legal[RIGHT] && first.legal[DOWN] &&
        first.legal[LEFT]);
  CHECK(first.selected_move == UP);
  CHECK(second.selected_move == first.selected_move);
  CHECK(second.values == first.values);
}

void test_terminal_root() {
  const beam2048::Board board{
      1, 2, 1, 2,
      2, 1, 2, 1,
      1, 2, 1, 2,
      2, 1, 2, 1,
  };
  board_t native{};
  copy_to_native(board, native);

  const beam2048::Result result = beam2048::search(
      native, beam2048::Config{3, 8, 1024},
      [](const beam2048::Board&) { return 0.0; });
  CHECK(result.selected_move == -1);
  for (int direction = UP; direction <= LEFT; ++direction) {
    CHECK(!result.legal[direction]);
    CHECK(std::isinf(result.values[direction]));
    CHECK(result.values[direction] < 0.0);
  }
  CHECK(result.stats.expanded_nodes == 0);
  CHECK(result.stats.generated_nodes == 0);
  CHECK(result.stats.evaluated_nodes == 0);
  CHECK(result.stats.pruned_nodes == 0);
  CHECK(result.stats.beam_layers == 0);
  CHECK(result.stats.effective_width_sum == 0);
  CHECK(result.stats.min_effective_width == 0);
  CHECK(result.stats.max_effective_width == 0);
  CHECK(result.stats.retained_nodes == 0);
}

template <typename Function>
void check_invalid_argument(Function function) {
  bool threw = false;
  try {
    function();
  } catch (const std::invalid_argument&) {
    threw = true;
  } catch (...) {
  }
  CHECK(threw);
}

void test_invalid_config() {
  board_t board{};
  board[5] = 1;
  const beam2048::Evaluator evaluator =
      [](const beam2048::Board&) { return 0.0; };

  check_invalid_argument([&] {
    beam2048::search(board, beam2048::Config{0, 1, 1024}, evaluator);
  });
  check_invalid_argument([&] {
    beam2048::search(board, beam2048::Config{-1, 1, 1024}, evaluator);
  });
  check_invalid_argument([&] {
    beam2048::search(board, beam2048::Config{1, 0, 1024}, evaluator);
  });
  check_invalid_argument([&] {
    beam2048::search(board, beam2048::Config{1, 1, 0}, evaluator);
  });
  check_invalid_argument([&] {
    beam2048::search(board, beam2048::Config{1, 1, -1}, evaluator);
  });
  check_invalid_argument([&] {
    beam2048::search(board, beam2048::Config{1, 1, 1024},
                     beam2048::Evaluator{});
  });
  check_invalid_argument([&] { beam2048::retention_limit(1, 0); });
}

void test_retention_limit_boundaries() {
  CHECK(beam2048::retention_limit(0, 100) == 1);
  CHECK(beam2048::retention_limit(1, 100) == 1);
  CHECK(beam2048::retention_limit(2, 100) == 1);
  CHECK(beam2048::retention_limit(3, 100) == 1);
  CHECK(beam2048::retention_limit(4, 100) == 2);
  CHECK(beam2048::retention_limit(8, 100) == 2);
  CHECK(beam2048::retention_limit(9, 100) == 3);
  CHECK(beam2048::retention_limit(15, 100) == 3);
  CHECK(beam2048::retention_limit(16, 100) == 4);

  // Config::width remains a hard cap even when the square-root limit is
  // larger. The maximum uint64_t case also exercises overflow-safe sqrt.
  CHECK(beam2048::retention_limit(100, 3) == 3);
  CHECK(beam2048::retention_limit(
            std::numeric_limits<std::uint64_t>::max(), 10) == 10);
}

template <typename Function>
void check_domain_error(Function function) {
  bool threw = false;
  try {
    function();
  } catch (const std::domain_error&) {
    threw = true;
  } catch (...) {
  }
  CHECK(threw);
}

void test_policy_width_formulas() {
  beam2048::Config config;
  config.width = 1000;

  config.width_policy = beam2048::WidthPolicy::Fixed;
  config.fixed_width = 7;
  CHECK(beam2048::policy_width_limit(config, 1, {}) == 7);
  config.width = 5;
  CHECK(beam2048::policy_width_limit(config, 20, {}) == 5);

  config.width = 1000;
  config.width_policy = beam2048::WidthPolicy::Sqrt;
  CHECK(beam2048::policy_width_limit(config, 1, {}) == 2);
  CHECK(beam2048::policy_width_limit(config, 2, {}) == 4);
  CHECK(beam2048::policy_width_limit(config, 3, {}) == 8);

  config.width_policy = beam2048::WidthPolicy::Cbrt;
  CHECK(beam2048::policy_width_limit(config, 1, {}) == 2);
  CHECK(beam2048::policy_width_limit(config, 2, {}) == 3);
  CHECK(beam2048::policy_width_limit(config, 3, {}) == 4);
  CHECK(beam2048::policy_width_limit(config, 4, {}) == 7);

  config.width_policy = beam2048::WidthPolicy::Power;
  config.alpha = 0.5;
  CHECK(beam2048::policy_width_limit(config, 1, {}) == 2);
  CHECK(beam2048::policy_width_limit(config, 2, {}) == 4);
  CHECK(beam2048::policy_width_limit(config, 3, {}) == 8);
  config.alpha = 0.0;
  CHECK(beam2048::policy_width_limit(config, 100, {}) == 1);

  config.width_policy = beam2048::WidthPolicy::Increasing;
  config.initial_width = 3.0;
  config.width_ratio = 1.5;
  CHECK(beam2048::policy_width_limit(config, 1, {}) == 3);
  CHECK(beam2048::policy_width_limit(config, 2, {}) == 5);
  CHECK(beam2048::policy_width_limit(config, 3, {}) == 7);

  config.width_policy = beam2048::WidthPolicy::Decreasing;
  config.initial_width = 8.0;
  config.width_ratio = 2.0;
  CHECK(beam2048::policy_width_limit(config, 1, {}) == 8);
  CHECK(beam2048::policy_width_limit(config, 2, {}) == 4);
  CHECK(beam2048::policy_width_limit(config, 3, {}) == 2);
  CHECK(beam2048::policy_width_limit(config, 4, {}) == 1);
  CHECK(beam2048::policy_width_limit(config, 5, {}) == 1);

  // Every exponential policy saturates at the hard cap before arithmetic can
  // overflow, even for the largest representable layer depth.
  const std::size_t huge_depth = std::numeric_limits<std::size_t>::max();
  config.width = 11;
  config.width_policy = beam2048::WidthPolicy::Sqrt;
  CHECK(beam2048::policy_width_limit(config, huge_depth, {}) == 11);
  config.width_policy = beam2048::WidthPolicy::Cbrt;
  CHECK(beam2048::policy_width_limit(config, huge_depth, {}) == 11);
  config.width_policy = beam2048::WidthPolicy::Power;
  config.alpha = 1.0;
  CHECK(beam2048::policy_width_limit(config, huge_depth, {}) == 11);
  config.width_policy = beam2048::WidthPolicy::Increasing;
  config.initial_width = 2.0;
  config.width_ratio = 2.0;
  CHECK(beam2048::policy_width_limit(config, huge_depth, {}) == 11);
  config.width_policy = beam2048::WidthPolicy::Decreasing;
  CHECK(beam2048::policy_width_limit(config, huge_depth, {}) == 1);

  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 0, {}); });
}

void test_adaptive_policy() {
  beam2048::Config config;
  config.width = 10;
  config.width_policy = beam2048::WidthPolicy::Adaptive;
  config.adaptive_sigma = 1.0;

  CHECK(beam2048::policy_width_limit(config, 1, {}) == 1);
  CHECK(beam2048::policy_width_limit(config, 1, {5.0, 5.0, 5.0}) == 3);
  config.width = 2;
  CHECK(beam2048::policy_width_limit(config, 1, {5.0, 5.0, 5.0}) == 2);

  config.width = 10;
  // mean=7, population stddev=sqrt(5), threshold is about 7.764.
  CHECK(beam2048::policy_width_limit(config, 1, {10.0, 8.0, 6.0, 4.0}) ==
        2);
  config.adaptive_sigma = 0.0;
  CHECK(beam2048::policy_width_limit(config, 1, {10.0, 8.0, 6.0, 4.0}) ==
        1);
  config.adaptive_sigma = 100.0;
  config.width = 3;
  CHECK(beam2048::policy_width_limit(config, 1, {10.0, 8.0, 6.0, 4.0}) ==
        3);

  check_domain_error([&] {
    beam2048::policy_width_limit(
        config, 1, {1.0, std::numeric_limits<double>::quiet_NaN()});
  });
  check_domain_error([&] {
    beam2048::policy_width_limit(
        config, 1, {1.0, std::numeric_limits<double>::infinity()});
  });
}

void test_policy_validation_and_names() {
  using beam2048::WidthPolicy;
  CHECK(beam2048::width_policy_name(WidthPolicy::Fixed) == "fixed");
  CHECK(beam2048::width_policy_name(WidthPolicy::Sqrt) == "sqrt");
  CHECK(beam2048::width_policy_name(WidthPolicy::Cbrt) == "cbrt");
  CHECK(beam2048::width_policy_name(WidthPolicy::Power) == "power");
  CHECK(beam2048::width_policy_name(WidthPolicy::Increasing) == "increasing");
  CHECK(beam2048::width_policy_name(WidthPolicy::Decreasing) == "decreasing");
  CHECK(beam2048::width_policy_name(WidthPolicy::Adaptive) == "adaptive");

  beam2048::Config config;
  config.width = 10;
  config.width_policy = WidthPolicy::Fixed;
  config.fixed_width = 0;
  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 1, {}); });
  config.width_policy = WidthPolicy::Power;
  config.alpha = -0.1;
  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 1, {}); });
  config.alpha = std::numeric_limits<double>::quiet_NaN();
  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 1, {}); });
  config.width_policy = WidthPolicy::Increasing;
  config.initial_width = 0.0;
  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 1, {}); });
  config.initial_width = 1.0;
  config.width_ratio = std::numeric_limits<double>::infinity();
  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 1, {}); });
  config.width_policy = WidthPolicy::Adaptive;
  config.adaptive_sigma = -1.0;
  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 1, {}); });
  config.width_policy = static_cast<WidthPolicy>(999);
  check_invalid_argument(
      [&] { beam2048::policy_width_limit(config, 1, {}); });
}

void test_policy_depth_and_determinism() {
  beam2048::Board board{};
  board[5] = 1;
  board_t native{};
  copy_to_native(board, native);

  beam2048::Config depth_two{2, 10, 1};
  depth_two.width_policy = beam2048::WidthPolicy::Fixed;
  depth_two.fixed_width = 2;
  const beam2048::Result two = beam2048::search(
      native, depth_two, [](const beam2048::Board&) { return 0.0; });
  CHECK(two.status == beam2048::SearchStatus::Completed);
  CHECK(two.stats.beam_layers == 4);
  CHECK(two.stats.effective_width_sum == 8);

  beam2048::Config depth_three = depth_two;
  depth_three.depth = 3;
  const beam2048::Result first = beam2048::search(
      native, depth_three, [](const beam2048::Board&) { return 0.0; });
  const beam2048::Result second = beam2048::search(
      native, depth_three, [](const beam2048::Board&) { return 0.0; });
  CHECK(first.status == beam2048::SearchStatus::Completed);
  CHECK(first.stats.beam_layers == 8);
  CHECK(first.stats.effective_width_sum == 16);
  CHECK(first.selected_move == second.selected_move);
  CHECK(first.values == second.values);
  CHECK(first.stats.generated_nodes == second.stats.generated_nodes);
  CHECK(first.stats.retained_nodes == second.stats.retained_nodes);
  CHECK(first.stats.pruned_nodes == second.stats.pruned_nodes);
  CHECK(first.stats.generated_nodes ==
        first.stats.retained_nodes + first.stats.pruned_nodes);
}

void test_non_finite_evaluator_and_deadline() {
  board_t board{};
  board[5] = 1;
  beam2048::Config config{1, 2, 1};
  config.fixed_width = 2;
  check_domain_error([&] {
    beam2048::search(board, config, [](const beam2048::Board&) {
      return std::numeric_limits<double>::quiet_NaN();
    });
  });
  check_domain_error([&] {
    beam2048::search(board, config, [](const beam2048::Board&) {
      return std::numeric_limits<double>::infinity();
    });
  });

  config.time_limit_ms = 0;
  check_invalid_argument([&] {
    beam2048::search(board, config,
                     [](const beam2048::Board&) { return 0.0; });
  });

  config.depth = 1;
  config.time_limit_ms =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  const beam2048::Result huge_deadline = beam2048::search(
      board, config, [](const beam2048::Board&) { return 0.0; });
  CHECK(huge_deadline.status == beam2048::SearchStatus::Completed);

  config.depth = 3;
  config.width = 100;
  config.fixed_width = 100;
  config.time_limit_ms = 1;
  const auto start = std::chrono::steady_clock::now();
  const beam2048::Result timed_out = beam2048::search(
      board, config, [](const beam2048::Board&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 0.0;
      });
  const auto elapsed = std::chrono::steady_clock::now() - start;
  CHECK(timed_out.status == beam2048::SearchStatus::TimedOut);
  CHECK(timed_out.selected_move == UP);
  CHECK(timed_out.stats.deadline_checks > 0);
  CHECK(timed_out.stats.evaluated_nodes <= 1);
  CHECK(elapsed < std::chrono::seconds(1));
}

std::uint64_t depth_two_evaluator_calls(
    const beam2048::Board& root, int root_direction) {
  int root_reward = 0;
  const beam2048::Board afterstate =
      move_board(root, root_direction, root_reward);
  if (root_reward < 0) return 0;

  std::uint64_t calls = 0;
  for (const beam2048::Spawn& spawn :
       beam2048::generate_spawns(afterstate)) {
    for (int direction = UP; direction <= LEFT; ++direction) {
      int reward = 0;
      move_board(spawn.board, direction, reward);
      if (reward >= 0) ++calls;
    }
  }
  return calls;
}

void test_width_pruning() {
  beam2048::Board board{};
  board[5] = 1;
  board_t native{};
  copy_to_native(board, native);

  const beam2048::Evaluator evaluator = [](const beam2048::Board& value) {
    double result = 0.0;
    for (std::size_t i = 0; i < value.size(); ++i) {
      result += static_cast<double>((i + 3) * (i + 1) * value[i]);
    }
    return result;
  };
  const beam2048::Result narrow =
      beam2048::search(native, beam2048::Config{2, 1, 1024}, evaluator);
  beam2048::Config capped_config{2, 2, 1024};
  capped_config.fixed_width = 2;
  const beam2048::Result capped =
      beam2048::search(native, capped_config, evaluator);
  beam2048::Config wide_config{2, 64, 1024};
  wide_config.fixed_width = 64;
  const beam2048::Result wide =
      beam2048::search(native, wide_config, evaluator);

  CHECK(narrow.stats.expanded_nodes == 4);
  CHECK(narrow.stats.generated_nodes == 120);
  CHECK(narrow.stats.pruned_nodes == 116);
  CHECK(narrow.stats.beam_layers == 4);
  CHECK(narrow.stats.effective_width_sum == 4);
  CHECK(narrow.stats.min_effective_width == 1);
  CHECK(narrow.stats.max_effective_width == 1);
  CHECK(narrow.stats.retained_nodes == 4);

  CHECK(capped.stats.expanded_nodes == 4);
  CHECK(capped.stats.generated_nodes == 120);
  CHECK(capped.stats.pruned_nodes == 112);
  CHECK(capped.stats.beam_layers == 4);
  CHECK(capped.stats.effective_width_sum == 8);
  CHECK(capped.stats.min_effective_width == 2);
  CHECK(capped.stats.max_effective_width == 2);
  CHECK(capped.stats.retained_nodes == 8);

  CHECK(wide.stats.expanded_nodes == 4);
  CHECK(wide.stats.generated_nodes == 120);
  CHECK(wide.stats.beam_layers == 4);
  CHECK(wide.stats.effective_width_sum == 256);
  CHECK(wide.stats.min_effective_width == 64);
  CHECK(wide.stats.max_effective_width == 64);
  CHECK(wide.stats.retained_nodes == 120);
  CHECK(wide.stats.pruned_nodes == 0);

  // width is applied independently to each root direction. With width one,
  // probability normalization makes each root value equal to its retained
  // candidate's value, rather than probability * value.
  const std::array<double, 4> expected_narrow =
      depth_two_width_one_oracle(board, 1024, evaluator);
  bool value_changed = false;
  for (int direction = UP; direction <= LEFT; ++direction) {
    CHECK(close_to(narrow.values[direction], expected_narrow[direction]));
    if (narrow.legal[direction] &&
        !close_to(narrow.values[direction], wide.values[direction])) {
      value_changed = true;
    }
  }
  CHECK(value_changed);
}

void test_two_candidate_layer_matches_depth_two_oracle() {
  const beam2048::Board board{
      3, 3, 1, 2,
      3, 2, 1, 3,
      3, 1, 2, 1,
      1, 2, 1, 2,
  };
  board_t native{};
  copy_to_native(board, native);

  const beam2048::Evaluator evaluator = [](const beam2048::Board& value) {
    double result = 0.0;
    for (std::size_t i = 0; i < value.size(); ++i) {
      result += 0.25 * static_cast<double>((i + 1) * value[i]);
    }
    return result;
  };
  constexpr int reward_scale = 1024;
  const std::array<double, 4> expected =
      depth_two_oracle(board, reward_scale, evaluator);
  beam2048::Config config{2, 64, reward_scale};
  config.fixed_width = 64;
  const beam2048::Result result =
      beam2048::search(native, config, evaluator);

  // LEFT produces exactly one empty cell. Its two spawn candidates cause at
  // least four evaluator calls, so floor(sqrt(calls)) retains both and this
  // direction is identical to exhaustive depth-two search.
  CHECK(result.legal[LEFT]);
  CHECK(depth_two_evaluator_calls(board, LEFT) >= 4);
  CHECK(close_to(result.values[LEFT], expected[LEFT], 1e-8));
}

void test_zero_call_terminal_layers_and_stats() {
  // Only RIGHT and LEFT are legal. Either root move creates one empty cell,
  // and both possible spawns are terminal. Each root/layer therefore has zero
  // evaluator calls and must still retain one candidate.
  const beam2048::Board board{
      3, 3, 3, 6,
      5, 6, 7, 4,
      6, 7, 4, 5,
      7, 4, 5, 6,
  };
  board_t native{};
  copy_to_native(board, native);

  constexpr int reward_scale = 1024;
  const beam2048::Result depth_two = beam2048::search(
      native, beam2048::Config{2, 32, reward_scale},
      [](const beam2048::Board&) { return 0.0; });
  CHECK(depth_two.selected_move == RIGHT);
  CHECK(!depth_two.legal[UP] && depth_two.legal[RIGHT] &&
        !depth_two.legal[DOWN] && depth_two.legal[LEFT]);
  CHECK(close_to(depth_two.values[RIGHT], 16.0 * reward_scale));
  CHECK(close_to(depth_two.values[LEFT], 16.0 * reward_scale));
  CHECK(depth_two.stats.expanded_nodes == 2);
  CHECK(depth_two.stats.generated_nodes == 4);
  CHECK(depth_two.stats.evaluated_nodes == 0);
  CHECK(depth_two.stats.pruned_nodes == 2);
  CHECK(depth_two.stats.beam_layers == 2);
  CHECK(depth_two.stats.effective_width_sum == 2);
  CHECK(depth_two.stats.min_effective_width == 1);
  CHECK(depth_two.stats.max_effective_width == 1);
  CHECK(depth_two.stats.retained_nodes == 2);

  const beam2048::Result depth_three = beam2048::search(
      native, beam2048::Config{3, 32, reward_scale},
      [](const beam2048::Board&) { return 0.0; });
  CHECK(depth_three.selected_move == RIGHT);
  CHECK(close_to(depth_three.values[RIGHT], 16.0 * reward_scale));
  CHECK(close_to(depth_three.values[LEFT], 16.0 * reward_scale));
  CHECK(depth_three.stats.expanded_nodes == 4);
  CHECK(depth_three.stats.generated_nodes == 6);
  CHECK(depth_three.stats.evaluated_nodes == 0);
  CHECK(depth_three.stats.pruned_nodes == 2);
  CHECK(depth_three.stats.beam_layers == 4);
  CHECK(depth_three.stats.effective_width_sum == 4);
  CHECK(depth_three.stats.min_effective_width == 1);
  CHECK(depth_three.stats.max_effective_width == 1);
  CHECK(depth_three.stats.retained_nodes == 4);

  const beam2048::Result repeated = beam2048::search(
      native, beam2048::Config{3, 32, reward_scale},
      [](const beam2048::Board&) { return 0.0; });
  CHECK(repeated.selected_move == depth_three.selected_move);
  CHECK(repeated.values == depth_three.values);
}

}  // namespace

int main() {
  init_movetable();

  test_spawn_generation();
  test_double_loader_small_files();
  test_double_evaluator_contract();
  test_depth_one_matches_greedy();
  test_q10_immediate_reward();
  test_unscaled_double_immediate_reward();
  test_deterministic_tie_break();
  test_terminal_root();
  test_invalid_config();
  test_retention_limit_boundaries();
  test_policy_width_formulas();
  test_adaptive_policy();
  test_policy_validation_and_names();
  test_policy_depth_and_determinism();
  test_non_finite_evaluator_and_deadline();
  test_width_pruning();
  test_two_candidate_layer_matches_depth_two_oracle();
  test_zero_call_terminal_layers_and_stats();

  if (failures != 0) {
    std::fprintf(stderr, "%d beam-search unit test(s) failed\n", failures);
    return 1;
  }
  std::printf("all beam-search unit tests passed\n");
  return 0;
}
