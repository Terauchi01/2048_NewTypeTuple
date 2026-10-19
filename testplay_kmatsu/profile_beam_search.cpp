#include "beam_search_fast.h"
#include "double_evaluator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint64_t kSampleMask = 255;

struct ProfileRun {
  const char* implementation = "";
  std::uint64_t wall_ns = 0;
  std::uint64_t evaluator_ns_estimate = 0;
  std::uint64_t evaluator_calls = 0;
  std::uint64_t generated = 0;
  std::uint64_t layers = 0;
  std::uint64_t effective_width_sum = 0;
};

std::uint64_t timer_pair_overhead_ns() {
  constexpr int kIterations = 20000;
  std::uint64_t total = 0;
  for (int i = 0; i < kIterations; ++i) {
    const auto start = Clock::now();
    const auto finish = Clock::now();
    total += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
            .count());
  }
  return total / kIterations;
}

ProfileRun run_once(const char* implementation, const board_t& board,
                    const beam2048::Config& config,
                    const beam2048::DoubleTupleEvaluator6& evaluator,
                    std::uint64_t clock_overhead) {
  std::uint64_t calls = 0;
  std::uint64_t sampled_evaluator_ns = 0;
  std::uint64_t sample_count = 0;
  const beam2048::Evaluator timed_evaluator = [&](const beam2048::Board& state) {
    ++calls;
    if ((calls & kSampleMask) == 0) {
      const auto start = Clock::now();
      const double value = evaluator.evaluate(state);
      const auto finish = Clock::now();
      const auto elapsed = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
              .count());
      sampled_evaluator_ns += elapsed > clock_overhead
                                  ? elapsed - clock_overhead
                                  : 0;
      ++sample_count;
      return value;
    }
    return evaluator.evaluate(state);
  };

  const auto start = Clock::now();
  const beam2048::Result result =
      std::string(implementation) == "fast"
          ? beam2048::search_fast(board, config, timed_evaluator)
          : beam2048::search(board, config, timed_evaluator);
  const auto finish = Clock::now();
  const std::uint64_t wall_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
          .count());
  const std::uint64_t evaluator_ns =
      sample_count == 0
          ? 0
          : std::min(wall_ns, sampled_evaluator_ns * calls / sample_count);

  return {implementation,
          wall_ns,
          evaluator_ns,
          result.stats.evaluated_nodes,
          result.stats.generated_nodes,
          result.stats.beam_layers,
          result.stats.effective_width_sum};
}

std::uint64_t median(std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

struct Candidate {
  double value;
  std::uint64_t order;
};

bool candidate_better(const Candidate& lhs, const Candidate& rhs) {
  if (lhs.value != rhs.value) return lhs.value > rhs.value;
  return lhs.order < rhs.order;
}

volatile std::uint64_t selection_sink = 0;

void benchmark_selection(std::size_t count, std::size_t width) {
  count = std::max(count, width + 1);
  std::vector<Candidate> source;
  source.reserve(count);
  std::uint64_t state = 0x9e3779b97f4a7c15ULL;
  for (std::size_t i = 0; i < count; ++i) {
    state ^= state << 7;
    state ^= state >> 9;
    source.push_back({static_cast<double>(state & 0xfffff), i});
  }

  constexpr int kTrials = 25;
  std::uint64_t full_sort_ns = 0;
  std::uint64_t partial_select_ns = 0;
  for (int trial = 0; trial < kTrials; ++trial) {
    std::vector<Candidate> candidates = source;
    auto start = Clock::now();
    std::stable_sort(candidates.begin(), candidates.end(), candidate_better);
    auto finish = Clock::now();
    full_sort_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
            .count());
    selection_sink += candidates.front().order;

    candidates = source;
    start = Clock::now();
    std::nth_element(candidates.begin(), candidates.begin() + width,
                     candidates.end(), candidate_better);
    candidates.resize(width);
    std::sort(candidates.begin(), candidates.end(), candidate_better);
    finish = Clock::now();
    partial_select_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start)
            .count());
    selection_sink += candidates.front().order;
  }

  std::printf("selection,candidates,%zu,retained,%zu,trials,%d,"
              "stable_sort_ns_per_trial,%llu,nth_plus_sort_ns_per_trial,%llu\n",
              count, width, kTrials,
              static_cast<unsigned long long>(full_sort_ns / kTrials),
              static_cast<unsigned long long>(partial_select_ns / kTrials));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 5) {
    std::fprintf(stderr,
                 "Usage: %s <evaluator.dat[.xz]> [depth=5] [width=256] [repeats=5]\n",
                 argv[0]);
    return 2;
  }
  const int depth = argc > 2 ? std::atoi(argv[2]) : 5;
  const std::size_t width =
      argc > 3 ? static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10))
               : 256;
  const int repeats = argc > 4 ? std::atoi(argv[4]) : 5;
  if (depth < 2 || width == 0 || repeats < 1) {
    std::fprintf(stderr, "depth must be >=2, width and repeats must be positive\n");
    return 2;
  }

  beam2048::DoubleTupleEvaluator6 evaluator;
  std::string error;
  if (!evaluator.load(argv[1], &error)) {
    std::fprintf(stderr, "failed to load evaluator: %s\n", error.c_str());
    return 1;
  }
  init_movetable();
  const std::uint64_t clock_overhead = timer_pair_overhead_ns();

  // Fixed, reproducible mid-game-like board with six empty cells.
  const board_t board{1, 2, 3, 4, 1, 2, 0, 3,
                      0, 1, 2, 0, 1, 0, 0, 0};
  beam2048::Config config;
  config.depth = depth;
  config.width = width;
  config.fixed_width = width;
  config.width_policy = beam2048::WidthPolicy::Fixed;
  config.time_limit_ms = 60000;

  std::vector<std::uint64_t> baseline_wall, baseline_eval, baseline_calls;
  std::vector<std::uint64_t> fast_wall, fast_eval, fast_calls;
  ProfileRun last_baseline, last_fast;
  for (int iteration = 0; iteration < repeats + 1; ++iteration) {
    // Discard the first pair as warm-up; alternate order to reduce drift bias.
    const bool fast_first = (iteration % 2) != 0;
    ProfileRun first = run_once(fast_first ? "fast" : "baseline", board,
                                config, evaluator, clock_overhead);
    ProfileRun second = run_once(fast_first ? "baseline" : "fast", board,
                                 config, evaluator, clock_overhead);
    if (std::strcmp(first.implementation, "baseline") == 0) {
      last_baseline = first;
    } else {
      last_fast = first;
    }
    if (std::strcmp(second.implementation, "baseline") == 0) {
      last_baseline = second;
    } else {
      last_fast = second;
    }
    if (iteration == 0) continue;
    for (const ProfileRun* run : {&first, &second}) {
      const bool is_fast = std::strcmp(run->implementation, "fast") == 0;
      auto& wall = is_fast ? fast_wall : baseline_wall;
      auto& eval = is_fast ? fast_eval : baseline_eval;
      auto& calls = is_fast ? fast_calls : baseline_calls;
      wall.push_back(run->wall_ns);
      eval.push_back(run->evaluator_ns_estimate);
      calls.push_back(run->evaluator_calls);
    }
  }

  const std::uint64_t base_wall = median(baseline_wall);
  const std::uint64_t base_eval = median(baseline_eval);
  const std::uint64_t fast_wall_time = median(fast_wall);
  const std::uint64_t fast_eval_time = median(fast_eval);
  std::printf("profile,depth,%d,width,%zu,repeats,%d,eval_sample_every,256,"
              "timer_pair_overhead_ns,%llu\n",
              depth, width, repeats,
              static_cast<unsigned long long>(clock_overhead));
  std::printf("implementation,median_wall_ns,median_evaluator_ns_est,"
              "median_other_ns_est,evaluator_calls_median,evaluator_pct\n");
  std::printf("baseline,%llu,%llu,%llu,%llu,%.2f\n",
              static_cast<unsigned long long>(base_wall),
              static_cast<unsigned long long>(base_eval),
              static_cast<unsigned long long>(base_wall - base_eval),
              static_cast<unsigned long long>(median(baseline_calls)),
              base_wall == 0 ? 0.0 : 100.0 * base_eval / base_wall);
  std::printf("fast,%llu,%llu,%llu,%llu,%.2f\n",
              static_cast<unsigned long long>(fast_wall_time),
              static_cast<unsigned long long>(fast_eval_time),
              static_cast<unsigned long long>(fast_wall_time - fast_eval_time),
              static_cast<unsigned long long>(median(fast_calls)),
              fast_wall_time == 0 ? 0.0
                                  : 100.0 * fast_eval_time / fast_wall_time);

  const std::uint64_t generated =
      (last_baseline.generated + last_fast.generated) / 2;
  const std::uint64_t layers =
      (last_baseline.layers + last_fast.layers) / 2;
  const std::uint64_t average_width_sum =
      (last_baseline.effective_width_sum + last_fast.effective_width_sum) / 2;
  if (layers > 0 && average_width_sum > 0) {
    benchmark_selection(
        std::max<std::size_t>(1, generated / layers),
        std::min<std::size_t>(width, average_width_sum / layers));
  }
  return 0;
}
