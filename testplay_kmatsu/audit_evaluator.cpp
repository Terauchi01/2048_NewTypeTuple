// Compare the production double expectimax implementation and beam evaluator.
// Reuse the game loop so all engines receive identical random tiles per seed.
#include "beam_search_afterstate.h"
#include "double_evaluator.h"
#include <unordered_map>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>

extern void input_ev(const char*);
extern double calcEvFiltered(const board_t&);
extern double expectimaxPlay(int, const board_t&,
    std::unordered_map<unsigned long long, double>&);
extern double Evs[2][beam2048::DoubleTupleEvaluator::kSplits]
    [beam2048::DoubleTupleEvaluator::kTuples][beam2048::DoubleTupleEvaluator::kArrayLength];

namespace beam2048 {
Result audit_reference_search(const board_t& board, const Config& config,
                              const Evaluator&) {
  Result result;
  std::unordered_map<unsigned long long, double> cache;
  double best = -std::numeric_limits<double>::infinity();
  for (int d = UP; d <= LEFT; ++d) {
    board_t next;
    const int reward = moveB(board, next, static_cast<move_dir>(d));
    if (reward < 0) continue;
    result.legal[d] = true;
    result.values[d] = reward + expectimaxPlay(config.depth, next, cache);
    if (result.values[d] > best) {
      best = result.values[d];
      result.selected_move = d;
    }
  }
  return result;
}
}
#define BEAM_SEARCH_FUNCTION audit_reference_search
#define main unused_beam_runner_main
#include "test_beam_search.cpp"
#undef main

int main(int argc, char** argv) {
  if (argc != 4 || (std::string(argv[1]) != "audit" &&
                    std::string(argv[1]) != "games" && std::string(argv[1]) != "calibrate")) {
    std::fprintf(stderr, "Usage: %s audit|games|calibrate evaluator.dat output_directory\n", argv[0]);
    return 2;
  }
  const std::filesystem::path out(argv[3]);
  std::filesystem::create_directories(out);
  if (std::filesystem::file_size(argv[2]) != beam2048::DoubleTupleEvaluator::kByteCount) {
    std::fprintf(stderr, "wrong raw-double file size\n");
    return 2;
  }
  input_ev(argv[2]);
  std::string error;
  // Check every value against the original expectimax loader, not just size.
  {
    beam2048::DoubleBuffer buffer;
    if (!beam2048::load_exact_doubles(argv[2], beam2048::DoubleTupleEvaluator::kValueCount, buffer, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str()); return 2;
    }
    if (std::memcmp(buffer.data(), Evs, beam2048::DoubleTupleEvaluator::kByteCount) != 0) {
      std::fprintf(stderr, "loader byte mismatch\n"); return 1;
    }
    std::size_t nonfinite = 0, nonzero = 0;
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (std::size_t i = 0; i < buffer.size; ++i) {
      const double v = buffer.data()[i];
      if (!std::isfinite(v)) ++nonfinite;
      if (v != 0) ++nonzero;
      lo = std::min(lo, v); hi = std::max(hi, v);
    }
    std::printf("loader,bytes,%zu,byte_identical,1,nonfinite,%zu,nonzero,%zu,min,%.17g,max,%.17g\n",
        beam2048::DoubleTupleEvaluator::kByteCount, nonfinite, nonzero, lo, hi);
    std::fflush(stdout);
    if (nonfinite != 0) return 1;
  }
  beam2048::DoubleTupleEvaluator evaluator;
  if (!evaluator.load(argv[2], &error)) {
    std::fprintf(stderr, "%s\n", error.c_str()); return 2;
  }
  init_movetable();
  if (std::string(argv[1]) == "calibrate") {
    std::ofstream timing(out / "calibration.csv");
    timing << "depth,moves,search_time_us,avg_search_us,status\n";
    timing.flush();
    std::printf("STARTED expectimax calibration seed=0 moves=16 target_us=1000000 threads=1\n");
    std::fflush(stdout);
    // The legacy implementation has key_depth[10]; never index past it.
    for (int depth = 1; depth <= 9; ++depth) {
      beam2048::Config config;
      config.depth = depth;
      const auto path = out / ("depth" + std::to_string(depth) + ".csv");
      FILE* file = std::fopen(path.c_str(), "w");
      if (!file) return 1;
      const auto outcome = run_game(0, config, 1, 16, evaluator, file);
      const bool closed = std::fclose(file) == 0;
      if (!closed || outcome.status == GameStatus::Error || outcome.moves == 0) return 1;
      std::ifstream input(path);
      std::string line;
      std::getline(input, line);
      std::istringstream fields(line);
      std::string key, value;
      long long total = -1;
      while (std::getline(fields, key, ',') && std::getline(fields, value, ',')) {
        if (key == "search_time_us") total = std::stoll(value);
      }
      if (total < 0) return 1;
      const double avg = static_cast<double>(total) / outcome.moves;
      timing << depth << ',' << outcome.moves << ',' << total << ','
             << std::fixed << std::setprecision(3) << avg << ','
             << game_status_name(outcome.status) << '\n';
      timing.flush();
      std::printf("depth=%d avg_search_us=%.3f\n", depth, avg);
      std::fflush(stdout);
      if (avg > 1000000.0) return 0;
    }
    std::fprintf(stderr, "1 second not reached before legacy safe depth limit 9\n");
    return 1;
  }
  if (std::string(argv[1]) == "games") {
    std::atomic<int> next{0};
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    // 30 full games: greedy and depth-3/4 expectimax, seeds 0..9.
    for (int t = 0; t < 30; ++t) workers.emplace_back([&] {
      for (;;) {
        const int job = next.fetch_add(1);
        if (job >= 30) return;
        const int depth = job / 10 == 0 ? 1 : (job / 10 == 1 ? 3 : 4);
        const std::string name = depth == 1 ? "greedy" : "expectimax" + std::to_string(depth);
        const auto path = out / (name + "_seed" + std::to_string(job % 10) + ".csv");
        FILE* file = std::fopen(path.c_str(), "w");
        if (!file) { failed.store(true); continue; }
        beam2048::Config config;
        config.depth = depth;
        const auto outcome = run_game(job % 10, config, 30, 0, evaluator, file);
        if (std::fclose(file) != 0 || outcome.status != GameStatus::Terminal) failed.store(true);
      }
    });
    std::printf("STARTED greedy=10 expectimax3=10 expectimax4=10 workers=30\n");
    std::fflush(stdout);
    for (auto& worker : workers) worker.join();
    return failed.load() ? 1 : 0;
  }

  std::mt19937 rng(2048);
  std::size_t leaf_checks = 0;
  double max_leaf_difference = 0;
  auto checked_evaluator = [&](const beam2048::Board& board) {
    board_t raw; std::copy(board.begin(), board.end(), raw);
    const double a = evaluator.evaluate(board), b = calcEvFiltered(raw);
    max_leaf_difference = std::max(max_leaf_difference, std::abs(a-b));
    ++leaf_checks;
    if (a != b) throw std::runtime_error("leaf evaluator differs from expectimax");
    return a;
  };
  for (int sample = 0; sample < 10000; ++sample) {
    beam2048::Board board;
    for (auto& tile : board) tile = rng() % 18;
    checked_evaluator(board);
  }
  std::ofstream csv(out / "root_values.csv");
  csv << "board,depth,method,move,reference,beam,difference,selected_reference,selected_beam,evaluated,pruned\n" << std::setprecision(17);
  const std::vector<beam2048::Board> boards{
    {1,1,2,3, 4,3,2,1, 5,4,3,0, 6,5,0,0},
    {11,10,9,8, 6,7,8,7, 5,4,3,2, 0,0,1,1},
    {14,13,12,11, 9,10,9,8, 7,6,5,4, 0,0,2,2}
  };
  const std::vector<beam2048::WidthPolicy> policies{
    beam2048::WidthPolicy::Fixed, beam2048::WidthPolicy::Sqrt,
    beam2048::WidthPolicy::SqrtBudget,
    beam2048::WidthPolicy::Cbrt, beam2048::WidthPolicy::Power,
    beam2048::WidthPolicy::Increasing, beam2048::WidthPolicy::Decreasing,
    beam2048::WidthPolicy::Adaptive
  };
  for (std::size_t n = 0; n < boards.size(); ++n) {
    board_t raw; std::copy(boards[n].begin(), boards[n].end(), raw);
    for (int depth : {3,4}) {
      beam2048::Config config;
      config.depth = depth;
      const auto reference = beam2048::audit_reference_search(raw, config, checked_evaluator);
      // No pruning must reproduce legacy expectimax, including merge rewards.
      config.width = config.fixed_width = 10000000;
      const auto full = beam2048::search_afterstate(raw, config, checked_evaluator);
      if (full.status != beam2048::SearchStatus::Completed || full.stats.pruned_nodes != 0) return 1;
      auto record = [&](const std::string& name, const beam2048::Result& result) {
        for (int d = 0; d < 4; ++d) if (reference.legal[d]) {
          csv << n << ',' << depth << ',' << name << ',' << d << ',' << reference.values[d] << ',' << result.values[d]
              << ',' << result.values[d]-reference.values[d] << ',' << reference.selected_move << ',' << result.selected_move
              << ',' << result.stats.evaluated_nodes << ',' << result.stats.pruned_nodes << '\n';
        }
        csv.flush();
      };
      record("unpruned", full);
      for (int d = 0; d < 4; ++d) if (reference.legal[d] && std::abs(full.values[d]-reference.values[d]) > 1e-6) {
        std::fprintf(stderr, "unpruned root differs board=%zu depth=%d move=%d\n", n, depth, d); return 1;
      }
      for (auto policy : policies) {
        config.width = 2048; config.fixed_width = 64;
        config.width_policy = policy; config.alpha = 0.4;
        config.initial_width = 64; config.width_ratio = 2; config.adaptive_sigma = 1;
        const auto result = beam2048::search_afterstate(raw, config, checked_evaluator);
        if (result.status != beam2048::SearchStatus::Completed) return 1;
        record(std::string(beam2048::width_policy_name(policy)), result);
      }
      std::printf("audit,board,%zu,depth,%d,unpruned_matches,1\n", n, depth);
      std::fflush(stdout);
    }
  }
  std::printf("leaf_checks,%zu,max_abs_difference,%.17g\n", leaf_checks, max_leaf_difference);
  return 0;
}
