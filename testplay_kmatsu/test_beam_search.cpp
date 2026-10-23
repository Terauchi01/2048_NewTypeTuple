#ifndef BEAM_SEARCH_FUNCTION
#ifdef BEAM_AFTERSTATE_RUNNER
#include "beam_search_afterstate.h"
#define BEAM_SEARCH_FUNCTION search_afterstate
#else
#include "beam_search.h"
#define BEAM_SEARCH_FUNCTION search
#endif
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
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
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
  if (text == "greedy") policy = WidthPolicy::Fixed;
  else if (text == "fixed") policy = WidthPolicy::Fixed;
  else if (text == "sqrt") policy = WidthPolicy::Sqrt;
  else if (text == "sqrt_budget") policy = WidthPolicy::SqrtBudget;
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
  std::int64_t search_time_us = 0;
  int depth_reached_min = 0, depth_reached_max = 0;
  bool all_frontiers_exhausted = false;
};

GameOutcome run_game(std::uint32_t game_seed, const beam2048::Config& config,
                     int thread_count, int max_moves,
                     const beam2048::DoubleTupleEvaluator& evaluator,
                     FILE* output = stdout, FILE* scores = nullptr,
                     std::string_view method_label = {}) {
  const auto game_start = std::chrono::steady_clock::now();
  const auto started_us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  std::mt19937 generator(game_seed);
  board_t board{};
  add_random_tile(board, generator);
  add_random_tile(board, generator);

  int score = 0;
  int moves = 0;
  beam2048::Stats total_stats;
  std::int64_t search_time_us = 0;
  GameStatus game_status = GameStatus::Terminal;
  int reached_min = std::numeric_limits<int>::max(), reached_max = 0;
  bool all_frontiers_exhausted = true;

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

    const int reached = result.max_depth_reached > 0 ? result.max_depth_reached : config.depth;
    reached_min = std::min(reached_min, reached);
    reached_max = std::max(reached_max, reached);
    all_frontiers_exhausted = all_frontiers_exhausted && result.frontier_exhausted;
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

  const auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now()-game_start).count();
  const auto finished_us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
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
      method_label.empty() ? beam2048::width_policy_name(config.width_policy) : method_label;
  std::fprintf(output,
      "game,%u,sco,%d,big,%d,turn,%d,moves,%d,status,%s,depth,%d,"
      "method,%.*s,max_width,%zu,k,%zu,alpha,%.9g,w0,%.9g,ratio,%.9g,"
      "sigma,%.9g,reward_scale,%d,threads,%d,max_moves,%d,timeout_ms,%llu,budget,%llu,"
      "expanded,%llu,generated,%llu,evaluated,%llu,pruned,%llu,"
      "beam_layers,%llu,effective_width_min,%zu,effective_width_max,%zu,"
      "effective_width_avg,%.3f,retained,%llu,deadline_checks,%llu,"
      "search_time_us,%lld,avg_search_us,%.3f,time_us,%lld,depth_reached_min,%d,depth_reached_max,%d,tuple_type,%d,wall_time_us,%lld,started_unix_us,%lld,finished_unix_us,%lld\n",
      game_seed, score, biggestTile(board), moves + 1, moves,
      game_status_name(game_status), config.depth,
      static_cast<int>(method.size()), method.data(), config.width,
      config.fixed_width, config.alpha, config.initial_width,
      config.width_ratio, config.adaptive_sigma, config.reward_scale,
      thread_count, max_moves,
      static_cast<unsigned long long>(config.time_limit_ms),
      static_cast<unsigned long long>(config.width_budget),
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
      static_cast<long long>(search_time_us),
      moves > 0 ? reached_min : 0, reached_max,
      beam2048::DoubleTupleEvaluator::kTupleType, static_cast<long long>(wall_us),
      static_cast<long long>(started_us),static_cast<long long>(finished_us));
  if (std::fflush(output) != 0) return {GameStatus::Error, moves};
  if (scores != nullptr) {
    const int written = std::fprintf(scores, "%.*s,%d,%u,%d,%llu,%d,%s,%lld,%d,%lld,%lld,%lld\n",
        static_cast<int>(method.size()), method.data(), config.depth, game_seed,
        score, 1ULL << biggestTile(board), moves, game_status_name(game_status),
        static_cast<long long>(search_time_us), beam2048::DoubleTupleEvaluator::kTupleType,
        static_cast<long long>(wall_us),static_cast<long long>(started_us),static_cast<long long>(finished_us));
    if (written < 0 || std::fflush(scores) != 0) {
      std::fprintf(stderr, "failed to save game score for seed %u\n", game_seed);
      return {GameStatus::Error, moves};
    }
  }
  return {game_status, moves, search_time_us, moves > 0 ? reached_min : 0, reached_max,
          moves > 0 && all_frontiers_exhausted};
}

void usage(const char* program) {
  std::fprintf(
      stderr,
      "Usage: %s <seed> <game_count> <depth> <method> <max_width> <evfile> "
      "[options]\n"
      "  method: greedy (depth 1) | fixed | sqrt | sqrt_budget | cbrt | power | increasing | decreasing | adaptive\n"
      "  depth: player moves including root; first post-root layer is d=1\n"
      "  max_width: hard cap applied to every method (>= 1)\n"
      "  evfile: double tuple seed0/count004 data, raw or .xz (requires xz)\n"
      "Options:\n"
      "  --budget N        sqrt_budget width = floor(sqrt(N)), capped by max_width (default 100000)\n"
      "  --calibrate-dir P measure all eight methods until target average per move\n"
      "  --max-depth N     calibration limit (default 128)\n"
      "  --target-ms N     calibration target milliseconds per move (default 1000)\n"
      "  --threads N       worker threads (default 5)\n"
      "  --scores-file PATH append and flush a score row after each game\n"
      "  --jobs-file PATH  TSV rows: seed, depth, method, output path; overrides positional jobs\n"
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

  std::string calibration_dir;
  int calibration_max_depth = 128;
  int calibration_target_ms = 1000;
  std::uint64_t width_budget = 100000;
  std::string scores_file;
  std::string jobs_file;
  if (std::string_view(argv[4]) == "greedy" && depth != 1) {
    std::fprintf(stderr, "greedy requires depth=1\n"); return EXIT_FAILURE;
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
    if (option == "--calibrate-dir") {
      calibration_dir = argv[index+1]; valid = !calibration_dir.empty();
    } else if (option == "--max-depth") {
      valid = parse_integer<int>(argv[index+1], 1, 512, calibration_max_depth);
    } else if (option == "--target-ms") {
      valid = parse_integer<int>(argv[index+1], 1, 60000, calibration_target_ms);
    } else if (option == "--budget") {
      valid = parse_integer<std::uint64_t>(argv[index+1], 1,
        std::numeric_limits<std::uint64_t>::max(), width_budget);
    } else if (option == "--scores-file") {
      scores_file = argv[index + 1];
      valid = !scores_file.empty();
    } else if (option == "--jobs-file") {
      jobs_file = argv[index + 1];
      valid = !jobs_file.empty();
    } else if (option == "--threads") {
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

  beam2048::DoubleTupleEvaluator evaluator;
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
  config.width_budget = width_budget;
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

  if (!calibration_dir.empty()) {
    if (max_moves == 0) {
      std::fprintf(stderr, "calibration requires a positive --max-moves\n");
      return EXIT_FAILURE;
    }
    std::filesystem::create_directories(calibration_dir);
    const beam2048::WidthPolicy policies[]{beam2048::WidthPolicy::Fixed,
      beam2048::WidthPolicy::Sqrt,beam2048::WidthPolicy::SqrtBudget,
      beam2048::WidthPolicy::Cbrt,beam2048::WidthPolicy::Power,
      beam2048::WidthPolicy::Increasing,beam2048::WidthPolicy::Decreasing,
      beam2048::WidthPolicy::Adaptive};
    const int worker_count = std::min(thread_count, 8);
    std::atomic<int> next{0};
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    for (int t=0;t<worker_count;++t) workers.emplace_back([&] {
      for (;;) {
        const int index=next.fetch_add(1);
        if (index>=8) return;
        auto method_config=config;
        method_config.width_policy=policies[index];
        const std::string method(beam2048::width_policy_name(policies[index]));
        const auto directory=std::filesystem::path(calibration_dir)/method;
        std::filesystem::create_directories(directory);
        std::ofstream timings(directory/"calibration.csv");
        timings << "method,depth,moves,search_time_us,avg_search_us,status,depth_reached_min,depth_reached_max\n";
        timings.flush();
        std::ofstream summary(directory/"selection.csv");
        summary << "method,selected_depth,avg_search_us,first_over_depth,status,budget\n";
        int selected=0;
        double selected_avg=0;
        bool stopped=false;
        for (int d=1;d<=calibration_max_depth;++d) {
          method_config.depth=d;
          const auto path=directory/("depth"+std::to_string(d)+".csv");
          FILE* file=std::fopen(path.c_str(),"w");
          if (!file) {failed.store(true);stopped=true;break;}
          const auto outcome=run_game(seed,method_config,worker_count,max_moves,evaluator,file);
          const bool closed=std::fclose(file)==0;
          const double avg=outcome.moves>0 ? static_cast<double>(outcome.search_time_us)/outcome.moves : 0;
          timings << method << ',' << d << ',' << outcome.moves << ',' << outcome.search_time_us
                  << ',' << std::fixed << std::setprecision(3) << avg << ',' << game_status_name(outcome.status) << ',' << outcome.depth_reached_min << ',' << outcome.depth_reached_max << '\n';
          timings.flush();
          if (!closed || outcome.status==GameStatus::Error) {failed.store(true);stopped=true;break;}
          if (outcome.status==GameStatus::TimedOut || avg>calibration_target_ms*1000.0) {
            summary << method << ',' << selected << ',' << selected_avg << ',' << d << ",crossed," << width_budget << '\n';
            stopped=true; break;
          }
          selected=d; selected_avg=avg;
          if (outcome.all_frontiers_exhausted) {
            summary << method << ',' << selected << ',' << selected_avg << ",,frontier_exhausted," << width_budget << '\n';
            stopped=true; break;
          }
        }
        if (!stopped) summary << method << ',' << selected << ',' << selected_avg << ",,depth_limit," << width_budget << '\n';
        summary.flush();
        if (!timings || !summary) failed.store(true);
        std::lock_guard<std::mutex> lock(output_mutex);
        std::printf("calibration method=%s selected_depth=%d avg_us=%.3f\n",method.c_str(),selected,selected_avg);
        std::fflush(stdout);
      }
    });
    std::printf("STARTED calibration methods=8 workers=%d moves=%d target_ms=%d max_depth=%d budget=%llu\n",
      worker_count,max_moves,calibration_target_ms,calibration_max_depth,static_cast<unsigned long long>(width_budget));
    std::fflush(stdout);
    for (auto& worker:workers) worker.join();
    return failed.load()?EXIT_FAILURE:EXIT_SUCCESS;
  }

  struct Job {
    std::uint32_t seed;
    beam2048::Config config;
    std::string output_path;
    std::string method_label;
  };
  std::vector<Job> jobs;
  if (!jobs_file.empty()) {
    std::ifstream input(jobs_file);
    if (!input) {
      std::fprintf(stderr, "cannot open jobs file: %s\n", jobs_file.c_str());
      return EXIT_FAILURE;
    }
    std::string line;
    while (std::getline(input, line)) {
      std::istringstream row(line);
      std::string seed_text, depth_text, method_text, output_path;
      Job job{0, config, {}, {}};
      if (!std::getline(row, seed_text, '\t') ||
          !std::getline(row, depth_text, '\t') ||
          !std::getline(row, method_text, '\t') ||
          !std::getline(row, output_path) || output_path.empty() ||
          !parse_integer<std::uint32_t>(seed_text.c_str(), 0,
              std::numeric_limits<std::uint32_t>::max(), job.seed) ||
          !parse_integer<int>(depth_text.c_str(), 1,
              std::numeric_limits<int>::max(), job.config.depth) ||
          !parse_policy(method_text, job.config.width_policy) ||
          (method_text == "greedy" && job.config.depth != 1)) {
        std::fprintf(stderr, "invalid jobs row: %s\n", line.c_str());
        return EXIT_FAILURE;
      }
      for (const auto& existing : jobs) {
        if (existing.output_path == output_path) {
          std::fprintf(stderr, "duplicate job output: %s\n", output_path.c_str());
          return EXIT_FAILURE;
        }
      }
      job.output_path = std::move(output_path);
      job.method_label = method_text;
      jobs.push_back(std::move(job));
    }
    if (input.bad() || jobs.empty() ||
        jobs.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      std::fprintf(stderr, "empty, unreadable or oversized jobs file\n");
      return EXIT_FAILURE;
    }
    game_count = static_cast<int>(jobs.size());
  }

  // One shared stream, protected by run_game's output_mutex. Flush each row
  // so completed games are visible while the other worker threads continue.
  std::unique_ptr<FILE, decltype(&std::fclose)> scores(nullptr, &std::fclose);
  if (!scores_file.empty()) {
    scores.reset(std::fopen(scores_file.c_str(), "a+"));
    if (!scores || std::fseek(scores.get(), 0, SEEK_END) != 0) {
      std::fprintf(stderr, "cannot open scores file: %s\n", scores_file.c_str());
      return EXIT_FAILURE;
    }
    const long size = std::ftell(scores.get());
    if (size < 0 || (size == 0 && std::fprintf(scores.get(),
        "method,depth,seed,score,max_tile,moves,status,search_time_us,tuple_type,wall_time_us,started_unix_us,finished_unix_us\n") < 0) ||
        std::fflush(scores.get()) != 0) {
      std::fprintf(stderr, "cannot initialize scores file: %s\n", scores_file.c_str());
      return EXIT_FAILURE;
    }
  }
  thread_count = std::min(thread_count, game_count);
  std::atomic<int> next_game{0};
  std::atomic<bool> timed_out{false};
  std::atomic<bool> failed{false};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int thread = 0; thread < thread_count; ++thread) {
    threads.emplace_back([&]() {
      while (!jobs.empty() || (!timed_out.load() && !failed.load())) {
        const int game_index = next_game.fetch_add(1);
        if (game_index >= game_count) return;
        FILE* output = stdout;
        if (!jobs.empty()) {
          output = std::fopen(jobs[game_index].output_path.c_str(), "w");
          if (output == nullptr) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::fprintf(stderr, "cannot open job output: %s\n",
                         jobs[game_index].output_path.c_str());
            failed.store(true);
            continue;
          }
        }
        const GameOutcome outcome = run_game(
            jobs.empty() ? seed + static_cast<std::uint32_t>(game_index)
                         : jobs[game_index].seed,
            jobs.empty() ? config : jobs[game_index].config, thread_count,
            max_moves, evaluator, output, scores.get(),
            jobs.empty() ? std::string_view(argv[4]) : std::string_view(jobs[game_index].method_label));
        if (output != stdout && std::fclose(output) != 0) failed.store(true);
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
