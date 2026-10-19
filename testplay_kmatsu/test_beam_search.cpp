#ifdef BEAM_AFTERSTATE_RUNNER
#include "beam_search_afterstate.h"
#define BEAM_SEARCH_FUNCTION search_afterstate
#else
#include "beam_search.h"
#define BEAM_SEARCH_FUNCTION search
#endif
#include "double_evaluator.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::mutex output_mutex;

template <typename Integer>
bool parse_integer(const char* text, Integer minimum, Integer maximum,
                   Integer& result) {
  if (text == nullptr || text[0] == '\0') return false;
  const std::string_view input(text);
  Integer parsed{};
  const auto conversion =
      std::from_chars(input.data(), input.data() + input.size(), parsed);
  if (conversion.ec != std::errc{} ||
      conversion.ptr != input.data() + input.size() || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  result = parsed;
  return true;
}

bool parse_double(const char* text, double minimum, double maximum,
                  double& result) {
  if (text == nullptr || text[0] == '\0') return false;
  errno = 0;
  char* end = nullptr;
  const double parsed = std::strtod(text, &end);
  if (errno == ERANGE || end == text || *end != '\0' ||
      !std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
    return false;
  }
  result = parsed;
  return true;
}

bool parse_policy(std::string_view text, beam2048::WidthPolicy& policy) {
  using beam2048::WidthPolicy;
  if (text == "fixed") policy = WidthPolicy::Fixed;
  else if (text == "sqrt") policy = WidthPolicy::Sqrt;
  else if (text == "cbrt") policy = WidthPolicy::Cbrt;
  else if (text == "power") policy = WidthPolicy::Power;
  else if (text == "increasing") policy = WidthPolicy::Increasing;
  else if (text == "decreasing") policy = WidthPolicy::Decreasing;
  else if (text == "adaptive") policy = WidthPolicy::Adaptive;
  else return false;
  return true;
}

int random_empty_cell(const board_t& board, std::mt19937& generator) {
  int empty_cells[16];
  int empty_count = 0;
  for (int index = 0; index < 16; ++index) {
    if (board[index] == 0) empty_cells[empty_count++] = index;
  }
  if (empty_count == 0) return -1;
  std::uniform_int_distribution<int> distribution(0, empty_count - 1);
  return empty_cells[distribution(generator)];
}

bool add_random_tile(board_t& board, std::mt19937& generator) {
  const int index = random_empty_cell(board, generator);
  if (index < 0) return false;
  std::uniform_int_distribution<int> tile_distribution(0, 9);
  board[index] = tile_distribution(generator) == 0 ? 2 : 1;
  return true;
}

enum class GameStatus { Terminal, MoveLimit, TimedOut, Error };

const char* game_status_name(GameStatus status) {
  switch (status) {
    case GameStatus::Terminal: return "terminal";
    case GameStatus::MoveLimit: return "move_limit";
    case GameStatus::TimedOut: return "timeout";
    case GameStatus::Error: return "error";
  }
  return "error";
}

struct GameOutcome {
  GameStatus status = GameStatus::Terminal;
  int moves = 0;
};

GameOutcome run_game(std::uint32_t game_seed, const beam2048::Config& config,
                     int thread_count, int max_moves,
                     const beam2048::DoubleTupleEvaluator6& evaluator) {
  std::mt19937 generator(game_seed);
  board_t board{};
  add_random_tile(board, generator);
  add_random_tile(board, generator);

  int score = 0;
  int moves = 0;
  beam2048::Stats total_stats;
  std::int64_t search_time_us = 0;
  GameStatus game_status = GameStatus::Terminal;

  while (max_moves == 0 || moves < max_moves) {
    const auto start = std::chrono::steady_clock::now();
    beam2048::Result result;
    try {
      result = beam2048::BEAM_SEARCH_FUNCTION(
          board, config,
          [&evaluator](const beam2048::Board& candidate) {
            return evaluator.evaluate(candidate);
          });
    } catch (const std::exception& exception) {
      std::lock_guard<std::mutex> lock(output_mutex);
      std::fprintf(stderr, "beam search failed for game %u: %s\n", game_seed,
                   exception.what());
      game_status = GameStatus::Error;
      break;
    }
    const auto finish = std::chrono::steady_clock::now();
    search_time_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(finish - start)
            .count();
    total_stats += result.stats;

    if (result.status == beam2048::SearchStatus::TimedOut) {
      game_status = GameStatus::TimedOut;
      break;
    }
    if (result.selected_move < 0) {
      game_status = GameStatus::Terminal;
      break;
    }

    board_t afterstate{};
    const int reward = moveB(
        board, afterstate, static_cast<move_dir>(result.selected_move));
    if (reward < 0) {
      std::lock_guard<std::mutex> lock(output_mutex);
      std::fprintf(stderr, "beam search selected an illegal move for game %u\n",
                   game_seed);
      game_status = GameStatus::Error;
      break;
    }
    std::copy(std::begin(afterstate), std::end(afterstate), std::begin(board));
    score += reward;
    ++moves;
    add_random_tile(board, generator);
  }
  if (game_status == GameStatus::Terminal && max_moves > 0 && moves >= max_moves) {
    game_status = GameStatus::MoveLimit;
  }

  std::lock_guard<std::mutex> lock(output_mutex);
  const double average_effective_width =
      total_stats.beam_layers == 0
          ? 0.0
          : static_cast<double>(total_stats.effective_width_sum) /
                static_cast<double>(total_stats.beam_layers);
  const double average_search_us =
      moves == 0 ? 0.0
                 : static_cast<double>(search_time_us) /
                       static_cast<double>(moves);
  const std::string_view method =
      beam2048::width_policy_name(config.width_policy);
  std::printf(
      "game,%u,sco,%d,big,%d,turn,%d,moves,%d,status,%s,depth,%d,"
      "method,%.*s,max_width,%zu,k,%zu,alpha,%.9g,w0,%.9g,ratio,%.9g,"
      "sigma,%.9g,reward_scale,%d,threads,%d,max_moves,%d,timeout_ms,%llu,"
      "expanded,%llu,generated,%llu,evaluated,%llu,pruned,%llu,"
      "beam_layers,%llu,effective_width_min,%zu,effective_width_max,%zu,"
      "effective_width_avg,%.3f,retained,%llu,deadline_checks,%llu,"
      "search_time_us,%lld,avg_search_us,%.3f,time_us,%lld\n",
      game_seed, score, biggestTile(board), moves + 1, moves,
      game_status_name(game_status), config.depth,
      static_cast<int>(method.size()), method.data(), config.width,
      config.fixed_width, config.alpha, config.initial_width,
      config.width_ratio, config.adaptive_sigma, config.reward_scale,
      thread_count, max_moves,
      static_cast<unsigned long long>(config.time_limit_ms),
      static_cast<unsigned long long>(total_stats.expanded_nodes),
      static_cast<unsigned long long>(total_stats.generated_nodes),
      static_cast<unsigned long long>(total_stats.evaluated_nodes),
      static_cast<unsigned long long>(total_stats.pruned_nodes),
      static_cast<unsigned long long>(total_stats.beam_layers),
      total_stats.min_effective_width, total_stats.max_effective_width,
      average_effective_width,
      static_cast<unsigned long long>(total_stats.retained_nodes),
      static_cast<unsigned long long>(total_stats.deadline_checks),
      static_cast<long long>(search_time_us), average_search_us,
      static_cast<long long>(search_time_us));
  std::fflush(stdout);
  return {game_status, moves};
}

void usage(const char* program) {
  std::fprintf(
      stderr,
      "Usage: %s <seed> <game_count> <depth> <method> <max_width> <evfile> "
      "[options]\n"
      "  method: fixed | sqrt | cbrt | power | increasing | decreasing | adaptive\n"
      "  depth: player moves including root; first post-root layer is d=1\n"
      "  max_width: hard cap applied to every method (>= 1)\n"
      "  evfile: double 6-tuple seed0/count004 data, raw or .xz (requires xz)\n"
      "Options:\n"
      "  --threads N       worker threads (default 5)\n"
      "  --max-moves N     stop normally after N moves; 0 means full game\n"
      "  --timeout-sec N   per-move deadline, 1..60 (default 60)\n"
      "  --k N             fixed width k (default max_width)\n"
      "  --alpha X         power exponent alpha (default 0.5)\n"
      "  --w0 X            initial increasing/decreasing width (default 1)\n"
      "  --ratio X         increasing/decreasing ratio (default 2)\n"
      "  --sigma X         adaptive population-stddev multiplier (default 1)\n",
      program);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 7) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  std::uint32_t seed = 0;
  int game_count = 0;
  int depth = 0;
  std::uint64_t width = 0;
  beam2048::WidthPolicy policy{};
  if (!parse_integer<std::uint32_t>(
          argv[1], 0, std::numeric_limits<std::uint32_t>::max(), seed) ||
      !parse_integer<int>(argv[2], 1, std::numeric_limits<int>::max(), game_count) ||
      !parse_integer<int>(argv[3], 1, std::numeric_limits<int>::max(), depth) ||
      !parse_policy(argv[4], policy) ||
      !parse_integer<std::uint64_t>(
          argv[5], 1,
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()),
          width)) {
    std::fprintf(stderr, "invalid required argument\n");
    usage(argv[0]);
    return EXIT_FAILURE;
  }
  if (static_cast<std::uint64_t>(seed) +
          static_cast<std::uint64_t>(game_count) - 1 >
      std::numeric_limits<std::uint32_t>::max()) {
    std::fprintf(stderr, "seed range exceeds uint32_t\n");
    return EXIT_FAILURE;
  }

  int thread_count = 5;
  int max_moves = 0;
  int timeout_seconds = 60;
  std::uint64_t fixed_width = width;
  double alpha = 0.5;
  double initial_width = 1.0;
  double ratio = 2.0;
  double sigma = 1.0;

  for (int index = 7; index < argc; index += 2) {
    if (index + 1 >= argc) {
      std::fprintf(stderr, "option %s requires a value\n", argv[index]);
      usage(argv[0]);
      return EXIT_FAILURE;
    }
    const std::string_view option(argv[index]);
    bool valid = false;
    if (option == "--threads") {
      valid = parse_integer<int>(argv[index + 1], 1,
                                 std::numeric_limits<int>::max(), thread_count);
    } else if (option == "--max-moves") {
      valid = parse_integer<int>(argv[index + 1], 0,
                                 std::numeric_limits<int>::max(), max_moves);
    } else if (option == "--timeout-sec") {
      valid = parse_integer<int>(argv[index + 1], 1, 60, timeout_seconds);
    } else if (option == "--k") {
      valid = parse_integer<std::uint64_t>(argv[index + 1], 1, width, fixed_width);
    } else if (option == "--alpha") {
      valid = parse_double(argv[index + 1], 0.0,
                           std::numeric_limits<double>::max(), alpha);
    } else if (option == "--w0") {
      valid = parse_double(argv[index + 1],
                           std::numeric_limits<double>::min(),
                           std::numeric_limits<double>::max(), initial_width);
    } else if (option == "--ratio") {
      valid = parse_double(argv[index + 1],
                           std::numeric_limits<double>::min(),
                           std::numeric_limits<double>::max(), ratio);
    } else if (option == "--sigma") {
      valid = parse_double(argv[index + 1], 0.0,
                           std::numeric_limits<double>::max(), sigma);
    } else {
      std::fprintf(stderr, "unknown option: %s\n", argv[index]);
      usage(argv[0]);
      return EXIT_FAILURE;
    }
    if (!valid) {
      std::fprintf(stderr, "invalid value for %s: %s\n", argv[index], argv[index + 1]);
      return EXIT_FAILURE;
    }
  }

  beam2048::DoubleTupleEvaluator6 evaluator;
  std::string load_error;
  if (!evaluator.load(argv[6], &load_error)) {
    std::fprintf(stderr, "failed to load evaluator data %s: %s\n", argv[6],
                 load_error.c_str());
    return EXIT_FAILURE;
  }
  init_movetable();

  beam2048::Config config;
  config.depth = depth;
  config.width = static_cast<std::size_t>(width);
  config.reward_scale = 1;
  config.width_policy = policy;
  config.fixed_width = static_cast<std::size_t>(fixed_width);
  config.alpha = alpha;
  config.initial_width = initial_width;
  config.width_ratio = ratio;
  config.adaptive_sigma = sigma;
  config.time_limit_ms = static_cast<std::uint64_t>(timeout_seconds) * 1000;
  try {
    (void)beam2048::policy_width_limit(config, 1, {});
  } catch (const std::exception& exception) {
    std::fprintf(stderr, "invalid beam configuration: %s\n", exception.what());
    return EXIT_FAILURE;
  }

  thread_count = std::min(thread_count, game_count);
  std::atomic<int> next_game{0};
  std::atomic<bool> timed_out{false};
  std::atomic<bool> failed{false};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int thread = 0; thread < thread_count; ++thread) {
    threads.emplace_back([&]() {
      while (!timed_out.load() && !failed.load()) {
        const int game_index = next_game.fetch_add(1);
        if (game_index >= game_count) return;
        const GameOutcome outcome = run_game(
            seed + static_cast<std::uint32_t>(game_index), config, thread_count,
            max_moves, evaluator);
        if (outcome.status == GameStatus::TimedOut) timed_out.store(true);
        if (outcome.status == GameStatus::Error) failed.store(true);
      }
    });
  }
  for (std::thread& thread : threads) thread.join();
  if (timed_out.load()) return 124;
  return failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;
}

#undef BEAM_SEARCH_FUNCTION
