#include "beam_search_fast.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
  init_movetable();
  board_t board{1, 2, 0, 0, 2, 3, 0, 0,
                0, 1, 2, 0, 0, 0, 0, 0};
  const beam2048::Evaluator evaluator = [](const beam2048::Board& candidate) {
    double value = 0.0;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
      value += static_cast<double>((i + 3) * (candidate[i] + 1));
    }
    return value;
  };

  const std::array<beam2048::WidthPolicy, 7> policies{
      beam2048::WidthPolicy::Fixed, beam2048::WidthPolicy::Sqrt,
      beam2048::WidthPolicy::Cbrt, beam2048::WidthPolicy::Power,
      beam2048::WidthPolicy::Increasing, beam2048::WidthPolicy::Decreasing,
      beam2048::WidthPolicy::Adaptive};
  for (const auto policy : policies) {
    beam2048::Config config;
    config.depth = 4;
    config.width = 64;
    config.fixed_width = 24;
    config.width_policy = policy;
    config.alpha = 0.4;
    config.initial_width = 8.0;
    config.width_ratio = 1.7;
    config.adaptive_sigma = 0.8;

    const beam2048::Result baseline =
        beam2048::search(board, config, evaluator);
    const beam2048::Result fast =
        beam2048::search_fast(board, config, evaluator);
    if (baseline.selected_move != fast.selected_move ||
        baseline.legal != fast.legal || baseline.status != fast.status ||
        baseline.stats.expanded_nodes != fast.stats.expanded_nodes ||
        baseline.stats.generated_nodes != fast.stats.generated_nodes ||
        baseline.stats.evaluated_nodes != fast.stats.evaluated_nodes ||
        baseline.stats.pruned_nodes != fast.stats.pruned_nodes ||
        baseline.stats.beam_layers != fast.stats.beam_layers ||
        baseline.stats.retained_nodes != fast.stats.retained_nodes ||
        baseline.stats.min_effective_width != fast.stats.min_effective_width ||
        baseline.stats.max_effective_width != fast.stats.max_effective_width) {
      std::fprintf(stderr, "policy parity failed: %.*s\n",
                   static_cast<int>(beam2048::width_policy_name(policy).size()),
                   beam2048::width_policy_name(policy).data());
      return 1;
    }
    for (std::size_t i = 0; i < baseline.values.size(); ++i) {
      if (baseline.values[i] != fast.values[i]) {
        std::fprintf(stderr, "value parity failed for direction %zu\n", i);
        return 1;
      }
    }
    if (baseline.stats.deadline_checks != fast.stats.deadline_checks) {
      std::fprintf(stderr, "deadline-check parity failed for policy %.*s\n",
                   static_cast<int>(beam2048::width_policy_name(policy).size()),
                   beam2048::width_policy_name(policy).data());
      return 1;
    }
  }
  std::puts("all baseline/fast parity tests passed");
  return 0;
}
