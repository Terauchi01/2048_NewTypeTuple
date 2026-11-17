// Frozen-model evaluation: no TDPlayer construction or learning calls.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include "game2048.h"
#include "tdplayer_VSE_symmetric2.h"
#include "exploration_policy.h"
#include "exploration_learning_rule.h"
#include "exploration_concurrency.h"

bool load_exploration_compact_state(const char*);

static unsigned long long integer(const std::string& s) {
  if(s.empty() || s.find_first_not_of("0123456789")!=std::string::npos)
    throw std::runtime_error("expected nonnegative integer");
  return std::stoull(s);
}
static void spawn(board_t& board,std::mt19937& rng) {
  int empty[16],count=0;
  for(int i=0;i<16;++i)if(!board[i])empty[count++]=i;
  if(!count)throw std::runtime_error("spawn on full board");
  const int cell=empty[std::uniform_int_distribution<int>(0,count-1)(rng)];
  board[cell]=std::uniform_int_distribution<int>(0,9)(rng)==0?2:1;
}
int main(int argc,char** argv) {
  try {
    if(argc<3)throw std::runtime_error(
      "usage: evaluate MODEL OUTPUT.csv [--games 200] [--seed 20261116] "
      "[--training-behavior tc-ucb] [--formula source] [--ucb-c .0065] "
      "[--warmup-steps 0] [--decay-end-steps 180000000000] [--model-access async]");
    unsigned long long games=200,seed=20261116;
    // These settings are required by the native checkpoint compatibility hash.
    // They do not govern action selection below, which is always greedy.
    exploration::config.mode=exploration::Mode::tc_ucb;
    exploration::config.formula=exploration::UcbFormula::source;
    exploration::config.c=exploration::scalar_t(.0065);
    exploration::config.decay_end_steps=180000000000LL;
    exploration_concurrency::active_mode=exploration_concurrency::Mode::async;
    learning_rule::active_schedule.mode=learning_rule::Mode::tc;
    for(int i=3;i<argc;i+=2) {
      if(i+1>=argc)throw std::runtime_error("option needs value");
      const std::string key=argv[i],value=argv[i+1];
      if(key=="--games")games=integer(value);
      else if(key=="--seed")seed=integer(value);
      else if(key=="--training-behavior")exploration::config.mode=exploration::parse_mode(value);
      else if(key=="--formula")exploration::config.formula=exploration::parse_formula(value);
      else if(key=="--ucb-c") {
        size_t used=0;const double coefficient=std::stod(value,&used);
        if(used!=value.size())throw std::runtime_error("invalid coefficient");
        exploration::config.c=static_cast<exploration::scalar_t>(coefficient);
      }
      else if(key=="--warmup-steps"||key=="--decay-end-steps") {
        const auto n=integer(value);
        if(n>static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
          throw std::runtime_error("step value too large");
        if(key=="--warmup-steps")exploration::config.warmup_steps=n;
        else exploration::config.decay_end_steps=n;
      }
      else if(key=="--model-access")exploration_concurrency::active_mode=exploration_concurrency::parse_mode(value);
      else throw std::runtime_error("unknown option: "+key);
    }
    if(!games || games>1000000 || seed>UINT32_MAX)throw std::runtime_error("invalid games/seed");
    if(std::filesystem::exists(argv[2]))throw std::runtime_error("refusing existing output");
    exploration::config.validate();learning_rule::active_schedule.validate();
    init_movetable();
    // The loader allocates and restores tables; init_tuple would allocate a
    // second initial state needlessly. All model checksums are verified.
    if(!load_exploration_compact_state(argv[1]))throw std::runtime_error("model load failed (check training configuration and precision)");
    exploration::prepare_model_access();
    std::ofstream out(argv[2]);
    if(!out)throw std::runtime_error("cannot open output");
    out<<"game,environment_seed,score,max_tile_exponent,turns,precision,initial_tiles\n";
    long long total=0;
    for(unsigned long long game=0;game<games;++game) {
      // Independent per-game streams let both models share the same random
      // numbers even when their earlier games have different lengths.
      std::seed_seq seq{static_cast<uint32_t>(seed),static_cast<uint32_t>(game),UINT32_C(0x4556414c)};
      std::mt19937 rng(seq);board_t board{};
      long long score=0,turns=0;
      // Match the learner's empty-board start: one spawn before first action.
      for(;;) {
        spawn(board,rng);
        int chosen=-1;td_value_t best=-std::numeric_limits<td_value_t>::infinity();
        board_t selected{};
        for(int d=0;d<4;++d) {
          board_t after{};const int reward=moveB(board,after,static_cast<move_dir>(d));
          if(reward<0)continue;
          const td_value_t value=static_cast<td_value_t>(reward)+calcEvFiltered(after);
          if(!std::isfinite(value))throw std::runtime_error("nonfinite evaluation");
          // Strict comparison preserves the learner's lowest-direction ties.
          if(chosen<0 || value>best) {
            chosen=d;best=value;std::copy(after,after+16,selected);
          }
        }
        if(chosen<0)break;
        board_t check{};const int reward=moveB(board,check,static_cast<move_dir>(chosen));
        score+=reward;++turns;std::copy(selected,selected+16,board);
        if(turns>=100000)throw std::runtime_error("game exceeded turn limit");
      }
      total+=score;
      out<<game<<','<<seed<<','<<score<<','<<biggestTile(board)<<','<<turns<<','
         <<(sizeof(td_value_t)==4?"float32":"float64")<<",1\n";
      out.flush();if(!out)throw std::runtime_error("evaluation CSV write failed");
    }
    std::cout<<"evaluation_complete,games,"<<games<<",mean_score,"<<double(total)/games<<'\n';
    return 0;
  }catch(const std::exception& e){std::cerr<<"evaluation failed: "<<e.what()<<'\n';return 1;}
}
