#ifndef EXPLORATION_OBSERVER_H
#define EXPLORATION_OBSERVER_H

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <ostream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace exploration_observer {

// These counters are worker-owned. Output and checkpoint synchronization belong
// to the driver; no RNG is consumed by aggregation.
struct Counters {
  uint64_t actions = 0, nongreedy = 0, legal = 0, original_games = 0;
  uint64_t paper_log_floor_actions = 0, paper_denominator_floor_decisions = 0,
           score_scale_floor_decisions = 0;
  std::array<uint64_t, 4> ranks{}, stage{};
  std::array<uint64_t, 8> progress{};
  double selected_bonus = 0, selected_min_a = 0;
  std::array<uint64_t, 2> reached{}, straight{}, square{}, other{};
  template<class Decision> void add(const Decision& d) {
    ++actions;
    paper_log_floor_actions += d.paper_log_floor_count;
    paper_denominator_floor_decisions += d.paper_denominator_floor;
    score_scale_floor_decisions += d.score_scale_floor;
    nongreedy += d.chosen != d.greedy;
    int rank = 0;
    for (int a = 0; a < 4; ++a) if (d.legal[a]) {
      ++legal;
      if (d.raw_score[a] > d.raw_score[d.chosen]) ++rank;
    }
    ++ranks[rank];
    if (d.stage >= 0 && d.stage < 4) ++stage[d.stage];
    const unsigned bin = d.progress <= 0 ? 0 :
      std::min(7u, static_cast<unsigned>(d.progress) / 8192u);
    ++progress[bin];
    selected_bonus += d.bonus[d.chosen];
    selected_min_a += d.minA[d.chosen];
  }
  // Dihedral invariant corner placement: an outer row/column, or a corner
  // 2x2 square. Ties in tile rank are broken by cell index, recorded in config.
  template<class Board> void placement(const Board& board, int point) {
    std::array<int,16> cells{};
    for (int i=0;i<16;++i) cells[i]=i;
    std::sort(cells.begin(), cells.end(), [&](int a,int b) {
      return board[a] != board[b] ? board[a] > board[b] : a < b;
    });
    int r0=cells[0]/4,c0=cells[0]%4;
    bool row=true,col=true;
    int rmin=r0,rmax=r0,cmin=c0,cmax=c0;
    for(int k=1;k<3;++k) {
      const int r=cells[k]/4,c=cells[k]%4;
      row &= r==r0; col &= c==c0;
      rmin=std::min(rmin,r);rmax=std::max(rmax,r);
      cmin=std::min(cmin,c);cmax=std::max(cmax,c);
    }
    ++reached[point];
    if((row&&(r0==0||r0==3)) || (col&&(c0==0||c0==3))) ++straight[point];
    else if((rmax<=1||rmin>=2)&&(cmax<=1||cmin>=2)) ++square[point];
    else ++other[point];
  }
  void save(std::ostream& s) const {
    s << actions << ' ' << nongreedy << ' ' << legal << ' ' << original_games << ' '
      << paper_log_floor_actions << ' ' << paper_denominator_floor_decisions << ' '
      << score_scale_floor_decisions << ' ';
    for(auto x:ranks)s<<x<<' ';
    for(auto x:stage)s<<x<<' ';
    for(auto x:progress)s<<x<<' ';
    s<<std::setprecision(17)<<selected_bonus<<' '<<selected_min_a<<' ';
    for(const auto* a:{&reached,&straight,&square,&other})for(auto x:*a)s<<x<<' ';
    s<<'\n';
  }
  void load(std::istream& s) {
    s>>actions>>nongreedy>>legal>>original_games
      >>paper_log_floor_actions>>paper_denominator_floor_decisions>>score_scale_floor_decisions;
    for(auto& x:ranks)s>>x;
    for(auto& x:stage)s>>x;
    for(auto& x:progress)s>>x;
    s>>selected_bonus>>selected_min_a;
    for(auto* a:{&reached,&straight,&square,&other})for(auto& x:*a)s>>x;
    if(!s)throw std::runtime_error("invalid observer state");
  }
  void csv(std::ostream& s) const {
    s<<actions<<','<<nongreedy<<','<<legal<<','<<original_games<<','
     <<paper_log_floor_actions<<','<<paper_denominator_floor_decisions<<','<<score_scale_floor_decisions;
    for(auto x:ranks)s<<','<<x;
    for(auto x:stage)s<<','<<x;
    for(auto x:progress)s<<','<<x;
    s<<','<<std::setprecision(17)<<selected_bonus<<','<<selected_min_a;
    for(const auto* a:{&reached,&straight,&square,&other})for(auto x:*a)s<<','<<x;
  }
  static std::string header() {
    return "actions,nongreedy,legal,original_games,paper_log_floor_actions,paper_denominator_floor_decisions,score_scale_floor_decisions,rank0,rank1,rank2,rank3,stage0,stage1,stage2,stage3,progress0_8191,progress8192_16383,progress16384_24575,progress24576_32767,progress32768_40959,progress40960_49151,progress49152_57343,progress57344_plus,selected_bonus_sum,selected_minA_sum,reached7200,reached14500,straight7200,straight14500,square7200,square14500,other7200,other14500";
  }
};

template<class Decision,class Board> std::string samples(const Decision& d, int worker,
    uint64_t episode, int restart, long long turn, uint64_t local_action,
    const std::string& behavior,const Board& board) {
  std::ostringstream s;
  s<<std::setprecision(17);
  for(int a=0;a<4;++a) if(d.legal[a]) {
    int rank=0;
    for(int b=0;b<4;++b) if(d.legal[b]&&d.raw_score[b]>d.raw_score[a])++rank;
    s<<worker<<','<<episode<<','<<restart<<','<<turn<<','<<local_action<<','
     <<d.completed_steps<<','<<behavior<<','<<d.stage<<','<<d.progress<<','<<a
     <<",1,"<<d.raw_score[a]<<','<<d.minA[a]<<','<<d.bonus[a]<<','<<rank<<','
     <<(a==d.chosen)<<','<<(a==d.greedy)<<','<<d.effective_c<<','<<d.score_scale<<','<<d.behavior_score[a]
     <<','<<d.paper_log_floor_count<<','<<d.paper_denominator_floor<<','<<d.score_scale_floor;
    for(int k=0;k<16;++k)s<<','<<board[k];
    s<<'\n';
  }
  return s.str();
}
} // namespace exploration_observer
#endif
