#ifndef TARGET_VARIANTS_H
#define TARGET_VARIANTS_H
#include "game2048.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace target_variants {
enum class Mode {control, policy, replay, all_actions, nstep};
inline Mode mode=Mode::control;
inline double temperature=32000;
inline unsigned horizon=3;
inline bool explicit_mode=false, explicit_temperature=false, explicit_horizon=false;
inline constexpr unsigned replay_capacity=4096, replay_batch=32;
#ifdef FULL_SPAWN_TEST_TABLE
inline constexpr int replay_min_tile=4; // Exercise actual replay in short flow tests.
#else
inline constexpr int replay_min_tile=14;
#endif
inline constexpr std::uint64_t replay_max_age=1000000;
inline Mode parse(const std::string& s){if(s=="control")return Mode::control;if(s=="policy-target")return Mode::policy;if(s=="replay")return Mode::replay;if(s=="all-actions-policy")return Mode::all_actions;if(s=="nstep")return Mode::nstep;throw std::invalid_argument("invalid target variant");}
inline const char* name(Mode m){switch(m){case Mode::control:return "control";case Mode::policy:return "policy-target";case Mode::replay:return "replay";case Mode::all_actions:return "all-actions-policy";case Mode::nstep:return "nstep";}throw std::invalid_argument("invalid target variant");}
inline void validate(){if(!std::isfinite(temperature)||temperature<=0||horizon<1||horizon>100)throw std::invalid_argument("positive finite temperature and nstep in [1,100] required");}
inline double policy_average(const std::vector<double>& values){if(values.empty())return 0;double mx=*std::max_element(values.begin(),values.end()),total=0,weights=0;for(double v:values){const double w=std::exp((v-mx)/temperature);weights+=w;total+=w*v;}return total/weights;}
template<class Move,class Value> double sampled_greedy(const board_t& state,Move move,Value value){double best=-std::numeric_limits<double>::infinity();for(int d=0;d<4;++d){board_t next;int reward=move(state,next,static_cast<move_dir>(d));if(reward>=0)best=std::max(best,reward+value(next));}return std::isfinite(best)?best:0;}
template<class Move,class Value> double policy_target(const board_t& after,Move move,Value value){int empty=0;for(int x:after)empty+=x==0;if(!empty)throw std::invalid_argument("afterstate requires empty cell");double total=0;for(int cell=0;cell<16;++cell)if(after[cell]==0)for(int tile=1;tile<=2;++tile){board_t state;std::copy(after,after+16,state);state[cell]=tile;std::vector<double> values;for(int d=0;d<4;++d){board_t next;int r=move(state,next,static_cast<move_dir>(d));if(r>=0)values.push_back(r+value(next));}total+=(tile==1?.9:.1)/empty*policy_average(values);}return total;}
struct Decision {std::array<int,16> state{};std::vector<std::array<int,16>> actions;};
inline thread_local std::vector<Decision> trajectory;
inline thread_local unsigned worker_index=0;
struct ReplayItem {std::array<int,16> before{},next_spawned{};bool terminal=false;std::uint64_t birth=0;};
struct WorkerReplay {std::vector<ReplayItem> items;std::uint64_t clock=0,next_insert=0,next_sample=0,updates=0;};
inline std::vector<WorkerReplay> replay;
inline void begin_worker(unsigned i){worker_index=i;}
inline void push(WorkerReplay& w,ReplayItem item){item.birth=++w.clock;if(w.items.size()<replay_capacity)w.items.push_back(item);else w.items[w.next_insert%replay_capacity]=item;++w.next_insert;}
inline const ReplayItem* sample(WorkerReplay& w){if(w.items.empty())return nullptr;const auto& item=w.items[w.next_sample++%w.items.size()];return w.clock-item.birth<=replay_max_age?&item:nullptr;}
inline void save(const std::filesystem::path& p){std::ofstream f(p);f<<"TARGET_REPLAY_V1 "<<replay.size()<<'\n';for(const auto& w:replay){f<<w.clock<<' '<<w.next_insert<<' '<<w.next_sample<<' '<<w.updates<<' '<<w.items.size()<<'\n';for(const auto& i:w.items){f<<i.terminal<<' '<<i.birth;for(int x:i.before)f<<' '<<x;for(int x:i.next_spawned)f<<' '<<x;f<<'\n';}}f.close();if(!f)throw std::runtime_error("replay save failed");}
inline void load(const std::filesystem::path& p,unsigned workers){std::ifstream f(p);std::string magic;unsigned n;if(!(f>>magic>>n)||magic!="TARGET_REPLAY_V1"||n!=workers)throw std::runtime_error("invalid replay header");replay.assign(n,{});for(auto& w:replay){std::size_t count;if(!(f>>w.clock>>w.next_insert>>w.next_sample>>w.updates>>count)||count>replay_capacity||w.next_insert!=w.clock||count!=std::min<std::uint64_t>(w.clock,replay_capacity))throw std::runtime_error("invalid replay state");w.items.resize(count);for(auto& i:w.items){if(!(f>>i.terminal>>i.birth)||i.birth>w.clock||!i.birth)throw std::runtime_error("invalid replay age");for(int& x:i.before)if(!(f>>x)||x<0||x>17)throw std::runtime_error("invalid replay tile");for(int& x:i.next_spawned)if(!(f>>x)||x<0||x>17)throw std::runtime_error("invalid replay tile");}}f>>std::ws;if(!f.eof())throw std::runtime_error("replay trailing data");}
}
#endif
