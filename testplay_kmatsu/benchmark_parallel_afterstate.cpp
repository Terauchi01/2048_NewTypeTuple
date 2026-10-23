#include "beam_search_afterstate.h"
#include "double_evaluator.h"
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <latch>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>
using Clock=std::chrono::steady_clock;
int main(int argc,char**argv){
 if(argc<3||argc>6){std::fprintf(stderr,"Usage: %s evaluator.dat output_dir [tasks=96] [repeats=3] [max_threads=32]\n",argv[0]);return 2;}
 const int tasks=argc>3?std::stoi(argv[3]):96,repeats=argc>4?std::stoi(argv[4]):3,max_threads=argc>5?std::stoi(argv[5]):32;
 if(tasks<1||tasks>10000||repeats<1||repeats>100||max_threads<1||max_threads>32)return 2;
 const std::filesystem::path out(argv[2]);std::filesystem::create_directories(out);
 beam2048::DoubleTupleEvaluator6 table;std::string error;
 if(!table.load(argv[1],&error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
 init_movetable();
 const std::vector<beam2048::Board> boards{
 {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
 {1,1,2,3,4,3,2,1,5,4,3,0,6,5,0,0},
 {11,10,9,8,6,7,8,7,5,4,3,2,0,0,1,1}};
 const beam2048::WidthPolicy policies[]{beam2048::WidthPolicy::Fixed,beam2048::WidthPolicy::Sqrt,beam2048::WidthPolicy::SqrtBudget};
 auto evaluate=[&](const beam2048::Board& b){return table.evaluate(b);};
 std::vector<std::vector<beam2048::Result>> reference(3);
 for(int policy=0;policy<3;++policy)for(const auto& b:boards){
  board_t raw;std::copy(b.begin(),b.end(),raw);
  beam2048::Config config;config.depth=12;config.width=2048;config.fixed_width=64;config.width_policy=policies[policy];
  auto result=beam2048::search_afterstate(raw,config,evaluate);
  if(result.status!=beam2048::SearchStatus::Completed)return 1;
  reference[policy].push_back(result);
 }
 std::ofstream details(out/"searches.csv"),summary(out/"rounds.csv");
 details<<"method,threads,repeat,task,board,time_us,evaluator_calls,selected\n";
 summary<<"method,threads,repeat,tasks,wall_seconds,searches_per_second,avg_search_ms,p50_search_ms,p95_search_ms\n"<<std::setprecision(9);
 struct Round{int policy,threads,repeat;};std::vector<Round> rounds;
 for(int repeat=0;repeat<repeats;++repeat){
  std::vector<Round> current;
  for(int p=0;p<3;++p)for(int n:{1,2,4,8,16,32})if(n<=max_threads)current.push_back({p,n,repeat});
  std::mt19937 rng(2048+repeat);std::shuffle(current.begin(),current.end(),rng);
  rounds.insert(rounds.end(),current.begin(),current.end());
 }
 std::printf("STARTED parallel benchmark methods=3 threads=1,2,4,8,16,32 max_threads=%d tasks_per_round=%d repeats=%d shared_table_bytes=%zu\n",max_threads,tasks,repeats,beam2048::DoubleTupleEvaluator6::kByteCount);std::fflush(stdout);
 for(const auto round:rounds){
  beam2048::Config config;config.depth=12;config.width=2048;config.fixed_width=64;config.width_policy=policies[round.policy];
  std::atomic<int> next{0};std::atomic<bool> failed{false};std::barrier ready(round.threads+1);std::latch warmed(round.threads);
  struct Sample{long long us=0;std::uint64_t calls=0;int selected=-1;};std::vector<Sample> samples(tasks);
  std::vector<std::thread> workers;
  for(int t=0;t<round.threads;++t)workers.emplace_back([&]{
   // Initialize thread-local caches before starting the clock.
   try{board_t raw;std::copy(boards[0].begin(),boards[0].end(),raw);(void)beam2048::search_afterstate(raw,config,evaluate);}
   catch(...){failed.store(true);}
   warmed.count_down();ready.arrive_and_wait();
   while(!failed.load()){
    const int job=next.fetch_add(1);if(job>=tasks)return;
    try{
     const auto& board=boards[job%3];board_t raw;std::copy(board.begin(),board.end(),raw);
     auto start=Clock::now();auto result=beam2048::search_afterstate(raw,config,evaluate);auto finish=Clock::now();
     const auto& expected=reference[round.policy][job%3];
     if(result.status!=beam2048::SearchStatus::Completed||result.values!=expected.values||result.selected_move!=expected.selected_move){failed.store(true);return;}
     samples[job]={std::chrono::duration_cast<std::chrono::microseconds>(finish-start).count(),result.stats.evaluated_nodes,result.selected_move};
    }catch(...){failed.store(true);return;}
   }
  });
  warmed.wait();auto start=Clock::now();ready.arrive_and_wait();for(auto& w:workers)w.join();auto finish=Clock::now();
  if(failed.load()){std::fprintf(stderr,"parallel search failed or differs from serial reference\n");return 1;}
  const std::string method(beam2048::width_policy_name(policies[round.policy]));std::vector<long long> times;long long total=0;
  for(int i=0;i<tasks;++i){auto sample=samples[i];times.push_back(sample.us);total+=sample.us;
   details<<method<<','<<round.threads<<','<<round.repeat<<','<<i<<','<<i%3<<','<<sample.us<<','<<sample.calls<<','<<sample.selected<<'\n';}
  std::sort(times.begin(),times.end());const double wall=std::chrono::duration<double>(finish-start).count();
  summary<<method<<','<<round.threads<<','<<round.repeat<<','<<tasks<<','<<wall<<','<<tasks/wall<<','<<static_cast<double>(total)/tasks/1000<<','<<times[times.size()/2]/1000.0<<','<<times[std::min(times.size()-1,times.size()*95/100)]/1000.0<<'\n';
  details.flush();summary.flush();if(!details||!summary)return 1;
  std::printf("completed method=%s threads=%d repeat=%d wall_seconds=%.3f\n",method.c_str(),round.threads,round.repeat,wall);std::fflush(stdout);
 }
}
