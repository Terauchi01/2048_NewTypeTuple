#include "beam_search_afterstate.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>
namespace beam2048 {
Result search_afterstate_baseline(const board_t&, const Config&, const Evaluator&);
}
int main() {
  init_movetable();
  const beam2048::WidthPolicy policies[]{beam2048::WidthPolicy::Fixed,
    beam2048::WidthPolicy::Sqrt,beam2048::WidthPolicy::SqrtBudget,beam2048::WidthPolicy::Cbrt,
    beam2048::WidthPolicy::Power,beam2048::WidthPolicy::Increasing,
    beam2048::WidthPolicy::Decreasing,beam2048::WidthPolicy::Adaptive};
  {
    beam2048::Config c; c.width=100000; c.width_policy=beam2048::WidthPolicy::SqrtBudget;
    for (auto [budget,expected] : std::vector<std::pair<std::uint64_t,std::size_t>>{
          {1,1},{3,1},{4,2},{15,3},{16,4},{100000,316}}) {
      c.width_budget=budget;
      if (beam2048::policy_width_limit(c,1,{})!=expected ||
          beam2048::policy_width_limit(c,99,{})!=expected) return 1;
    }
    c.width=64;c.width_budget=100000;
    if(beam2048::policy_width_limit(c,1,{})!=64) return 1;
    c.width_budget=0;bool rejected=false;
    try{(void)beam2048::policy_width_limit(c,1,{});}
    catch(const std::invalid_argument&){rejected=true;}
    if(!rejected)return 1;
  }
  std::mt19937 rng(2048);
  int cases = 0;
  std::uint64_t before_calls=0, after_calls=0;
  for(int sample=0;sample<15;++sample) {
    board_t board{};
    if(sample==0) { board[0]=1; board[15]=1; }
    else for(auto& tile:board) tile=rng()%8;
    if(sample==1) board[0]=14; // exercise high-stage board contents
    for(auto policy:policies) for(int depth:{1,3,4,8}) for(int mode:{0,1,2}) {
      beam2048::Config config;
      config.depth=depth;config.width=64;config.fixed_width=16;
      config.width_policy=policy;config.initial_width=16;config.alpha=0.4;
      auto evaluator=[mode](const beam2048::Board& b) {
        if(mode==0) return 0.0; // all ties; generation order must be preserved
        std::uint64_t hash=1469598103934665603ULL;
        for(auto tile:b){hash^=static_cast<unsigned>(tile);hash*=1099511628211ULL;}
        return (mode==1?-1.0:1.0)*static_cast<double>(hash%200001);
      };
      auto reference=beam2048::search_afterstate_baseline(board,config,evaluator);
      auto actual=beam2048::search_afterstate(board,config,evaluator);
      if(reference.status!=actual.status || reference.selected_move!=actual.selected_move ||
         reference.legal!=actual.legal || reference.values!=actual.values ||
         reference.stats.generated_nodes!=actual.stats.generated_nodes ||
         reference.stats.pruned_nodes!=actual.stats.pruned_nodes ||
         reference.stats.retained_nodes!=actual.stats.retained_nodes) {
        std::fprintf(stderr,"parity failure sample=%d depth=%d mode=%d policy=%.*s\n",sample,depth,mode,
          static_cast<int>(beam2048::width_policy_name(policy).size()),beam2048::width_policy_name(policy).data());
        return 1;
      }
      before_calls+=reference.stats.evaluated_nodes;
      after_calls+=actual.stats.evaluated_nodes;
      ++cases;
    }
  }
  // A fresh search must not reuse values from another evaluator, including NaN.
  board_t board{}; board[0]=board[15]=1;
  beam2048::Config config; config.depth=3;config.width=config.fixed_width=16;
  (void)beam2048::search_afterstate(board,config,[](const beam2048::Board&){return 1.;});
  bool rejected=false;
  try {(void)beam2048::search_afterstate(board,config,[](const beam2048::Board&){return std::numeric_limits<double>::quiet_NaN();});}
  catch(const std::domain_error&) {rejected=true;}
  if(!rejected) return 1;
  std::printf("PASS %d exact parity cases; before_calls=%llu after_calls=%llu; evaluator isolation and NaN rejection passed\n",
    cases,static_cast<unsigned long long>(before_calls),static_cast<unsigned long long>(after_calls));
}
