#include "../testplay_kmatsu/beam_search_afterstate.h"
#include "../testplay_kmatsu/double_evaluator.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
namespace beam2048 {Result search_afterstate_baseline(const board_t&,const Config&,const Evaluator&);}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 beam2048::DoubleTupleEvaluator6 table;std::string error;
 if(!table.load(argv[1],&error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
 init_movetable();
 std::vector<beam2048::Board> boards{{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
 {1,1,2,3,4,3,2,1,5,4,3,0,6,5,0,0},
 {11,10,9,8,6,7,8,7,5,4,3,2,0,0,1,1}};
 std::printf("board,depth,implementation,repeat,time_us,evaluator_calls,generated,selected\n");
 for(std::size_t b=0;b<boards.size();++b)for(int depth:{4,8,12}){
  board_t raw;std::copy(boards[b].begin(),boards[b].end(),raw);
  beam2048::Config config;config.depth=depth;config.width=config.fixed_width=64;
  beam2048::Result reference;
  for(int repeat=-1;repeat<5;++repeat)for(int engine=0;engine<2;++engine){
   auto begin=std::chrono::steady_clock::now();
   auto evaluator=[&](const beam2048::Board& v){return table.evaluate(v);};
   auto result=engine?beam2048::search_afterstate(raw,config,evaluator):beam2048::search_afterstate_baseline(raw,config,evaluator);
   auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
   if(!engine)reference=result;
   else if(result.values!=reference.values||result.selected_move!=reference.selected_move)return 1;
   if(repeat>=0)std::printf("%zu,%d,%s,%d,%lld,%llu,%llu,%d\n",b,depth,engine?"optimized":"baseline",repeat,
    static_cast<long long>(us),static_cast<unsigned long long>(result.stats.evaluated_nodes),
    static_cast<unsigned long long>(result.stats.generated_nodes),result.selected_move);
  }
 }
}
