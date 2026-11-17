#include "exploration_policy.h"
#include "exploration_learning_rule.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>

static bool close(double a, double b) {
  return std::fabs(a-b) <= 1e-13 * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
}

int main() {
  using namespace exploration;
  const std::array<bool, 4> legal = {true, true, false, false};
  const std::array<double, 4> scores = {100, 99, 0, 0};
  const std::array<double, 4> history = {100, 4, 0, 0};
  std::mt19937 rng(491);
  const auto original_rng = rng;
  bind_worker(&rng, 3);
  set_step(0);
  config = {};
  assert(config.formula == UcbFormula::paper);
  auto decision = choose(legal, scores, history);
  assert(decision.chosen == 0 && decision.greedy == 0);
  config.mode = Mode::tc_ucb;
  config.c = 0;
  decision = choose(legal, scores, history);
  assert(decision.chosen == 0 && decision.bonus[0] == 0);
  config.c = 2;
  config.formula = UcbFormula::paper;
  decision = choose(legal, scores, history);
  assert(close(decision.bonus[0], 2*std::sqrt(std::log(100.)/104.)));
  assert(close(decision.bonus[1], 2*std::sqrt(std::log(4.)/104.)));
  assert(close(decision.behavior_score[0], 1+decision.bonus[0]));
  assert(close(decision.score_scale, 100));
  assert(decision.chosen == 0 && decision.greedy == 0 && !decision.legal[2]);
  // The diagnostic standard formula has a distinct uncertainty direction.
  config.formula = UcbFormula::standard;
  const auto standard = choose(legal, scores, history);
  assert(close(standard.bonus[0], 2*std::sqrt(std::log1p(104.)/100.)));
  assert(close(standard.bonus[1], 2*std::sqrt(std::log1p(104.)/4.)));
  assert(standard.chosen == 1 && standard.greedy == 0);

  // The historical source variant smooths the denominator by one and uses
  // maximum legal score normalization, including negative-valued candidates.
  config.formula = parse_formula("source");
  assert(config.formula == UcbFormula::source);
  assert(std::string(formula_name(config.formula)) == "source");
  config.validate();
  const auto source = choose(legal, scores, history);
  assert(close(source.bonus[0], 2*std::sqrt(std::log1p(104.)/101.)));
  assert(close(source.bonus[1], 2*std::sqrt(std::log1p(104.)/5.)));
  assert(source.chosen == 1 && source.greedy == 0);
  const auto equal_source = choose(legal, {100,100,0,0}, history);
  assert(equal_source.chosen == 1);
  const auto unseen_source = choose(legal, scores, {0,4,0,0});
  assert(close(unseen_source.bonus[0], 2*std::sqrt(std::log1p(4.))));
  assert(unseen_source.bonus[0] > unseen_source.bonus[1]);
  // Unlike the epsilon-denominator diagnostic, source remains bounded as
  // minA tends to zero, and a tiny positive history already gives a bonus.
  const auto tiny_source = choose(legal, scores, {1e-12,.5,0,0});
  assert(tiny_source.bonus[0] > tiny_source.bonus[1]);
  assert(tiny_source.bonus[1] > 0);
  assert(!tiny_source.paper_log_floor_count && !tiny_source.paper_denominator_floor);
  config.c = 0;
  const auto disabled_source = choose(legal, scores, history);
  assert(disabled_source.chosen == disabled_source.greedy);
  assert(disabled_source.behavior_score[0] == scores[0]);
  config.c = 2;

  const std::array<double, 4> zeros{};
  for (auto formula : {UcbFormula::paper, UcbFormula::standard, UcbFormula::source}) {
    config.formula = formula;
    auto zero = choose(legal, zeros, zeros);
    assert(zero.chosen == 0 && zero.score_scale == 1);
    assert(zero.score_scale_floor);
    assert(zero.paper_log_floor_count == (formula == UcbFormula::paper ? 2 : 0));
    assert(zero.paper_denominator_floor == (formula == UcbFormula::paper));
    for (int action = 0; action < 2; ++action)
      assert(std::isfinite(zero.behavior_score[action]));
    const auto negative = choose(legal, {-10,-11,0,0}, zeros);
    assert(negative.chosen == 0);
    assert(negative.score_scale == (formula == UcbFormula::standard ? 11 : 1));
    const auto mixed = choose(legal, {-100,2,0,0}, zeros);
    assert(mixed.chosen == 1);
    assert(mixed.score_scale == (formula == UcbFormula::standard ? 100 : 2));
  }
  config.formula = UcbFormula::paper;
  const auto floor = choose(legal, scores, {1e-12,.5,0,0});
  assert(floor.bonus[0] == 0 && floor.bonus[1] == 0);
  assert(floor.paper_log_floor_count == 2 && !floor.paper_denominator_floor);
  const auto reset = choose(legal, scores, {1e-12,1e-12,0,0});
  assert(reset.chosen == reset.greedy && reset.bonus[0] == 0);
  assert(reset.paper_log_floor_count == 2 && !reset.paper_denominator_floor);

  config.warmup_steps = 10;
  config.decay_end_steps = 20;
  assert(config.coefficient(9) == 0 && config.coefficient(10) == 2);
  assert(config.coefficient(15) == 1 && config.coefficient(20) == 0);
  set_step(15);
  decision = choose(legal, scores, history);
  assert(decision.effective_c == 1 && decision.worker_id == 3);
  assert(rng == original_rng);  // Neither policy consumes the environment RNG.
  bool rejected = false;
  config.decay_end_steps = config.warmup_steps;
  try { config.validate(); } catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);

  double weight = 0, error = 1e-12, absolute = 1e-12;
  const double expected = 2*.5*std::fabs(error+2)/(absolute+2);
  learning_rule::update(weight, error, absolute, 2, learning_rule::Rule::tc);
  assert(weight == expected && error > 2 && absolute > 2);
  std::puts("exploration_policy,passed,paper_log_ratio,source_log_ratio,guards,schedule,deterministic_rng,legacy_tc");
}
