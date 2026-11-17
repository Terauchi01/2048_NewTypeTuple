#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "game2048.h"
#include "symmetric.h"
#include "tdplayer_VSE_symmetric2.h"
#include "exploration_learning_rule.h"
#include "exploration_policy.h"
#include "exploration_concurrency.h"
#include "exploration_candidate_cache.h"
#include "exploration_io_guard.h"
#include "exploration_observer.h"
#include "start_board_pool.h"
#include "random_source_archive.h"
#include "archive_episode_accounting.h"
#include "archive_experience_balance.h"

#ifndef EXPLORATION_SOURCE_SHA256
#define EXPLORATION_SOURCE_SHA256 ""
#endif

namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
constexpr long long TC_RESET_STEPS=50000000000LL;
constexpr int RESTARTS=10, MAX_TURNS=100000;
#ifdef NT9_FLOAT32_STORAGE
constexpr const char* MODEL_PRECISION="float32";
#else
constexpr const char* MODEL_PRECISION="float64";
#endif
extern void resetErrs();
#ifdef NT9_COMPACT_STORAGE
bool save_exploration_compact_state(const char* filename);
bool load_exploration_compact_state(const char* filename);
#endif

struct Settings {
  int seed=0,threads=10;
  int feature_cache_mib=0;
  long long steps=200000000000LL,first=1000000,period=50000000000LL,
            summary=1000000,sample_every=100000,warmup=0,decay=0,extra_checkpoint=0;
  exploration::scalar_t c=static_cast<exploration::scalar_t>(.0065);
  std::string experiment_id="exploration";
  std::string behavior="greedy",formula="paper",source=EXPLORATION_SOURCE_SHA256;
  std::string model_access="serialized",checkpoint_format="dense";
  fs::path output="exploration_run",resume;
  fs::path checkpoint_io_lock,stop_after_checkpoint_file,save_and_stop_file;
  fs::path start_pool_path;
  std::string start_pool_sha256;
  fs::path start_pool_manifest;
  std::string start_pool_manifest_sha256;
  int start_pool_per_million=0;
  int archive_source_episodes=0;
  std::string archive_start_policy="requests",archive_condition="custom";
  bool archive_source_update=false;
  std::string archive_mode="random128";
  int archive_promote32_percent=20,archive_promote64_percent=50;
  long long checkpoint_io_timeout_seconds=43200;
  bool retain=true;
  std::string run_name() const { return experiment_id+"__seed_"+std::to_string(seed); }
  std::string identity() const {
    std::ostringstream s;s<<std::setprecision(17)
      <<"experiment "<<run_name()<<" seed "<<seed<<" threads "<<threads<<" first "<<first<<" period "<<period
      <<" summary "<<summary<<" sample_every "<<sample_every
      <<" behavior "<<behavior<<" formula "<<formula<<" c "<<c
      <<" warmup "<<warmup<<" decay "<<decay<<" source "<<source
      <<" model_access "<<model_access<<" checkpoint_format "<<checkpoint_format
      <<" feature_cache_mib "<<feature_cache_mib
      <<" tc_reset "<<TC_RESET_STEPS<<" restarts "<<RESTARTS
      <<" tuple "<<TUPLE_FILE_TYPE<<" rule legacy-tc-0.5-post teacher greedy-max";
    if(start_pool_per_million) s<<" start_pool_sha256 "<<start_pool_sha256
      <<" start_pool_manifest_sha256 "<<start_pool_manifest_sha256
      <<" start_pool_per_million "<<start_pool_per_million;
    if(start_pool_per_million)s<<" archive_mode "<<archive_mode<<" archive_accounting absolute-turn-score-v1";
    if(extra_checkpoint) s<<" extra_checkpoint "<<extra_checkpoint;
    if(archive_source_episodes)s<<" random_source128_v1 original-plus-restarts-per-use chance9to1-independent normal-window1000-promote32ge"<<archive_promote32_percent<<"-promote64gt"<<archive_promote64_percent<<"-count32 monotonic16k32k64k fallback64to-new32 source-turn-inherited next-source-latest1-per-stage reject-identical-last-or-pending starvation-normal-fallback";
    s<<" experience_balance_v1 start_policy "<<archive_start_policy<<" source_update "<<archive_source_update;
    s<<" model_precision "<<MODEL_PRECISION;
    return s.str();
  }
};
Settings cfg;
std::mutex modelMutex,outputMutex;
std::atomic<long long> completed{0};
std::atomic<unsigned long long> episodes{0};
std::atomic<bool> workerFailed{false},saveStopRequested{false};
std::mutex requestMutex;
std::string saveStopNonce,selfBoot;
unsigned long long rejectedSaveStopRequests=0;
unsigned long long selfTick=0;
long long waveTarget=0,resetEpoch=0,nextPeriodic=0;
unsigned long long checkpointOrdinal=0;
double priorSeconds=0;
Clock::time_point started;
std::string binaryHash;
std::ofstream summaryFile,episodeFile,sampleFile,statusFile;
std::ofstream poolFile,allocationFile,availabilityFile;
archive_experience_balance::Scheduler experienceBudget;
struct NamedPool {std::string sha; start_pool::Pool data;};
std::vector<NamedPool> startPools;
random_source_archive::Archive onlineArchive;

bool save_model(const fs::path& path) {
  if(cfg.checkpoint_format=="compact") {
#ifdef NT9_COMPACT_STORAGE
    return save_exploration_compact_state(path.c_str());
#else
    throw std::runtime_error("compact checkpoint requires NT9 compact storage");
#endif
  }
  return save_full_training_state(path.c_str());
}
bool load_model(const fs::path& path) {
  if(cfg.checkpoint_format=="compact") {
#ifdef NT9_COMPACT_STORAGE
    return load_exploration_compact_state(path.c_str());
#else
    throw std::runtime_error("compact checkpoint requires NT9 compact storage");
#endif
  }
  return load_full_training_state(path.c_str());
}

bool valid_hash(const std::string& s) {
  return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;
}
std::string sha256_file(const fs::path& p) {
  int fd=open(p.c_str(),O_RDONLY|O_CLOEXEC);
  if(fd<0)throw std::runtime_error("cannot open hash input: "+p.string());
  const std::string command="sha256sum /proc/"+std::to_string(getpid())+"/fd/"+std::to_string(fd);
  FILE* pipe=popen(command.c_str(),"r");
  if(!pipe){close(fd);throw std::runtime_error("cannot run sha256sum");}
  char line[256]{};bool ok=fgets(line,sizeof(line),pipe)!=nullptr;
  int code=pclose(pipe);close(fd);
  std::string result=ok?std::string(line,64):"";
  if(code||!valid_hash(result))throw std::runtime_error("sha256 failed: "+p.string());
  return result;
}
void sync_path(const fs::path& p,bool dir=false) {
  int fd=open(p.c_str(),O_RDONLY|O_CLOEXEC|(dir?O_DIRECTORY:0));
  if(fd<0)throw std::runtime_error("cannot open for fsync: "+p.string());
  int rc=fsync(fd);close(fd);
  if(rc)throw std::runtime_error("fsync failed: "+p.string());
}
void checked_close(std::ofstream& f,const std::string& what) {
  f.close();if(!f)throw std::runtime_error("write failed: "+what);
}
std::string json_string(const std::string& s) {
  std::string o="\"";
  for(unsigned char c:s) {
    if(c=='"'||c=='\\'){o+='\\';o+=char(c);}
    else if(c=='\n')o+="\\n";
    else if(c<32)throw std::runtime_error("control character in JSON string");
    else o+=char(c);
  }
  return o+'"';
}
std::string csv_string(const std::string& s) {
  std::string out="\"";
  for(char c:s) { if(c=='\"') out+='\"'; out+=c; }
  return out+'\"';
}
struct Worker {
  std::mt19937 environment,explore,diagnostic;
  unsigned long long episode=0,localActions=0;
  long long localSteps=0,windowSteps=0;
  exploration_observer::Counters total,window;
  std::unique_ptr<TDPlayer> player;
  void initialize(int worker) {
    auto seed_rng=[&](std::mt19937& rng,unsigned tag) {
      std::seed_seq seq{static_cast<unsigned>(cfg.seed),static_cast<unsigned>(worker),tag};
      rng.seed(seq);
    };
    seed_rng(environment,0x454e5601u);seed_rng(explore,0x45585002u);
    seed_rng(diagnostic,0x44494103u);
    player=std::make_unique<TDPlayer>();
  }
};
std::vector<std::unique_ptr<Worker>> workers;

void allocation_snapshot(int id) {
  const auto a=experienceBudget.snapshot();
  const auto steps=a.normal_steps+a.archive_steps, starts=a.normal_starts+a.archive_starts;
  allocationFile<<id<<','<<completed.load()<<','<<a.normal_steps<<','<<a.archive_steps<<','
    <<std::setprecision(17)<<(steps?double(a.archive_steps)/steps:0)<<','
    <<a.requested_normal<<','<<a.requested_archive<<','<<a.normal_starts<<','<<a.archive_starts<<','
    <<(starts?double(a.archive_starts)/starts:0)<<','<<a.archive_fallbacks<<','
    <<a.normal_completed<<','<<a.archive_completed<<','<<a.inflight_normal<<','<<a.inflight_archive<<','
    <<cfg.archive_start_policy<<','<<(cfg.archive_start_policy=="steps"?"0.5":"")<<','
    <<(cfg.archive_start_policy=="requests"?std::to_string(double(cfg.start_pool_per_million)/1000000.0):"")<<','<<cfg.archive_source_update<<'\n';
  if(!allocationFile)throw std::runtime_error("experience allocation CSV write failed");
}
void flush_window(int id,bool force) {
  Worker& w=*workers[id];
  if(!w.windowSteps || (!force&&w.windowSteps<cfg.summary))return;
  std::lock_guard<std::mutex> lock(outputMutex);
  summaryFile<<id<<','<<completed.load()<<','<<w.localSteps<<','<<w.windowSteps<<',';
  w.window.csv(summaryFile);summaryFile<<'\n';
  if(!summaryFile)throw std::runtime_error("summary CSV write failed");
  allocation_snapshot(id);
  w.window={};w.windowSteps=0;
}
void spawn(board_t& b,std::mt19937& rng) {
  int empty[16],n=0;
  for(int k=0;k<16;++k)if(!b[k])empty[n++]=k;
  if(!n)throw std::runtime_error("spawn on full board");
  const int k=empty[std::uniform_int_distribution<int>(0,n-1)(rng)];
  b[k]=std::uniform_int_distribution<int>(0,9)(rng)==0?2:1;
}
unsigned long long save_stop_process_starttick() {
  std::ifstream in("/proc/self/stat");std::string text;std::getline(in,text);const auto end=text.rfind(')');
  if(end==std::string::npos)throw std::runtime_error("cannot read own process identity");
  std::istringstream fields(text.substr(end+2));std::string field;for(int i=0;i<=19;++i)if(!(fields>>field))throw std::runtime_error("truncated own process identity");
  return std::stoull(field);
}
void reject_save_stop(const std::string& reason){
  ++rejectedSaveStopRequests;
  if((rejectedSaveStopRequests&(rejectedSaveStopRequests-1))==0)std::cerr<<"save_and_stop_rejected,count,"<<rejectedSaveStopRequests<<",reason,"<<reason<<std::endl;
}
void poll_save_stop() {
  std::lock_guard<std::mutex> lock(requestMutex);
  if(saveStopRequested.load()||cfg.save_and_stop_file.empty())return;
  std::error_code ec;const bool exists=fs::exists(cfg.save_and_stop_file,ec);
  if(ec){reject_save_stop("request stat failed: "+ec.message());return;}
  if(!exists)return;
  struct stat st{};if(lstat(cfg.save_and_stop_file.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()){reject_save_stop("untrusted request file");return;}
  std::ifstream in(cfg.save_and_stop_file);std::string magic,boot,out,binary,source,nonce;unsigned long long pid,tick,uid;
  in>>magic>>pid>>tick>>uid>>std::quoted(boot)>>std::quoted(out)>>std::quoted(binary)>>std::quoted(source)>>std::quoted(nonce);
  if(!in){reject_save_stop("malformed request");return;}in>>std::ws;
  if(!in.eof()||magic!="EXPLORATION_SAVE_STOP_V1"||pid!=static_cast<unsigned long long>(getpid())||tick!=selfTick||uid!=getuid()||boot!=selfBoot||out!=fs::canonical(cfg.output).string()||binary!=binaryHash||source!=cfg.source||nonce.size()!=32||nonce.find_first_not_of("0123456789abcdef")!=std::string::npos)
    {reject_save_stop("request identity mismatch");return;}
  saveStopNonce=nonce;saveStopRequested.store(true);
  std::cout<<"save_and_stop_accepted,nonce,"<<nonce<<",actual_steps,"<<completed.load()<<std::endl;
}
void run_worker(int id) {
 try {
  Worker& w=*workers[id];TDPlayer& player=*w.player;
  exploration::bind_worker(&w.explore,id);
  // Persistent player allocation survives checkpoint waves; only episode
  // histories are reset, never optimizer or RNG state.
  std::vector<std::array<int,16>> history(MAX_TURNS);
  std::vector<int> scoreHistory(MAX_TURNS);
  while(!workerFailed.load()&&!saveStopRequested.load(std::memory_order_relaxed)&&completed.load()<waveTarget) {
    const auto episodeIndex=w.episode;
    board_t board{};int score=0,turn=1,restartStart=0,restart=0;
    start_pool::Entry onlineEntry;unsigned long long onlineGeneration=0;std::string onlineSha;
    bool requestedArchive=false;unsigned seenTargets=0;
    const auto episodeStartSteps=completed.load();
    const double episodeStartSeconds=priorSeconds+std::chrono::duration<double>(Clock::now()-started).count();
    std::string fallbackReason="none";
    const start_pool::Entry* poolEntry=nullptr;
    const NamedPool* selectedPool=nullptr;
    // Keep the baseline environment RNG draw unchanged, including in steps mode.
    const bool coin=cfg.start_pool_per_million &&
       std::uniform_int_distribution<int>(0,999999)(w.environment)<cfg.start_pool_per_million;
    if(cfg.archive_start_policy=="steps")requestedArchive=experienceBudget.choose_steps(id);
    else {requestedArchive=coin;experienceBudget.begin(id,requestedArchive);}
    if(requestedArchive) {
      if(cfg.archive_source_episodes){
        if(onlineArchive.draw(w.environment,onlineEntry,onlineGeneration,onlineSha,id,completed.load(),&fallbackReason))poolEntry=&onlineEntry;
      }else{
        selectedPool=&startPools[std::uniform_int_distribution<size_t>(0,startPools.size()-1)(w.environment)];
        poolEntry=&selectedPool->data.draw(w.environment);
      }
      if(poolEntry)start_pool::copy_board(*poolEntry,board);
    }
    experienceBudget.commit(id,poolEntry!=nullptr);
    const int sourceTile=poolEntry?*std::max_element(poolEntry->board.begin(),poolEntry->board.end()):0;
    const long long sourceTurn=poolEntry?poolEntry->source_turn:0;
    const long long sourceScore=poolEntry?poolEntry->cumulative_score:0;
    int originalScore=0,originalTile=0,originalTurn=0;
    long long episodeSteps=0;
    player.gameStart();
    if(poolEntry&&cfg.archive_source_update)player.set_archive_source_afterstate(board);
    for(;;) {
      spawn(board,w.environment);
      alldir_bool legal{};alldir_board next{};alldir_int rewards{};
      bool any=false;
      for(int d=0;d<4;++d){rewards[d]=moveB(board,next[d],(move_dir)d);legal[d]=rewards[d]>=0;any|=legal[d];}
      if(!any) {
        {
          std::unique_lock<std::mutex> lock(modelMutex,std::defer_lock);
          if(cfg.model_access=="serialized")lock.lock();
          player.gameEnd();
        }
        if(restart==0){originalScore=score;originalTile=biggestTile(board);originalTurn=turn;++w.total.original_games;++w.window.original_games;if(cfg.archive_source_episodes)onlineArchive.record_original(!poolEntry,originalTile,completed.load());}
        const long long segment=turn-restartStart;
        experienceBudget.record_segment(id,segment);
        completed.fetch_add(segment);w.localSteps+=segment;w.windowSteps+=segment;episodeSteps+=segment;
        flush_window(id,false);
        // A spawned terminal archive board has no action history to restart.
        if(turn==1&&restart==0){restart=1;break;}
        if(++restart==RESTARTS)break;
        restartStart=(restartStart+turn)/2;turn=restartStart;
        if(turn<1||turn>=MAX_TURNS)throw std::runtime_error("invalid restart point");
        std::copy(history[turn].begin(),history[turn].end(),board);
        score=scoreHistory[turn];
        any=false;
        for(int d=0;d<4;++d){rewards[d]=moveB(board,next[d],(move_dir)d);legal[d]=rewards[d]>=0;any|=legal[d];}
        if(!any)throw std::runtime_error("restart restored terminal afterstate");
      }
      if(turn>=MAX_TURNS)throw std::runtime_error("game history capacity exceeded");
      // Saved afterstates retain their original action number. Learning and
      // restart/history indices continue to count only this continuation.
      const long long absoluteTurn=archive_episode_accounting::add(sourceTurn,turn);
      const int point=archive_episode_accounting::placement_point(absoluteTurn);
      if(restart==0&&point>=0) {
        w.total.placement(board,point);w.window.placement(board,point);
      }
      int selected;
      exploration::Decision decision;
      {
        // Both policies share the selected access contract. Async mode is
        // permitted only with the core's atomic reads and striped updates.
        std::unique_lock<std::mutex> lock(modelMutex,std::defer_lock);
        if(cfg.model_access=="serialized")lock.lock();
        exploration::set_step(completed.load());
        selected=player.selectHand(board,legal,next,rewards);
        decision=exploration::last_decision();
      }
      if(selected<0||selected>3||!legal[selected])throw std::runtime_error("illegal behavior action");
      w.total.add(decision);w.window.add(decision);++w.localActions;
      if(cfg.sample_every>0&&std::uniform_int_distribution<long long>(1,cfg.sample_every)(w.diagnostic)==1) {
        const auto rows=exploration_observer::samples(decision,id,episodeIndex,restart,absoluteTurn,w.localActions,cfg.behavior,board);
        std::lock_guard<std::mutex> lock(outputMutex);sampleFile<<rows;
        if(!sampleFile)throw std::runtime_error("decision sample CSV write failed");
      }
      if(cfg.archive_source_episodes&&!poolEntry&&restart==0&&staged_archive_curriculum::needs_capture(*std::max_element(next[selected],next[selected]+16),seenTargets)){
        start_pool::Entry entry;
        entry.board_id=cfg.run_name()+"_w"+std::to_string(id)+"_e"+std::to_string(episodeIndex);
        entry.board_id+="_s"+std::to_string(completed.load());
        entry.source_game=entry.board_id;entry.source_turn=turn;entry.cumulative_score=score+rewards[selected];
        std::copy(next[selected],next[selected]+16,entry.board.begin());
        onlineArchive.capture(std::move(entry),completed.load(),true,restart,seenTargets);
      }
      std::copy(next[selected],next[selected]+16,board);
      std::copy(board,board+16,history[turn].begin());
      score+=rewards[selected];scoreHistory[turn]=score;++turn;
    }
    experienceBudget.end_episode(id);
    ++w.episode;episodes.fetch_add(1);
    if(cfg.archive_source_episodes)onlineArchive.complete(id,completed.load());
    std::lock_guard<std::mutex> lock(outputMutex);
    episodeFile<<id<<','<<episodeIndex<<','<<completed.load()<<','<<w.localSteps<<','<<episodeSteps<<','
      <<originalScore<<','<<originalTile<<','<<originalTurn<<','<<restart<<','<<(poolEntry?"pool":"empty")
      <<','<<sourceTurn<<','<<sourceScore<<','<<archive_episode_accounting::add(sourceScore,originalScore)
      <<','<<archive_episode_accounting::add(sourceTurn,originalTurn)
      <<','<<requestedArchive<<','<<fallbackReason<<','<<episodeStartSteps<<','<<sourceTile<<','
      <<player.archive_source_update_count()<<','<<episodeStartSeconds<<','
      <<priorSeconds+std::chrono::duration<double>(Clock::now()-started).count()<<'\n';
    if(!episodeFile)throw std::runtime_error("episode CSV write failed");
    if(cfg.start_pool_per_million) {
      poolFile<<id<<','<<episodeIndex<<','<<(poolEntry?"pool":"empty")<<',';
      if(poolEntry) poolFile<<(cfg.archive_source_episodes?onlineSha:selectedPool->sha)<<','<<csv_string(poolEntry->board_id)<<','<<csv_string(poolEntry->source_game)
        <<','<<poolEntry->source_turn<<','<<poolEntry->cumulative_score;
      else poolFile<<",,,,";
      if(cfg.archive_source_episodes)poolFile<<','<<(requestedArchive?1:0)<<','<<(requestedArchive&&!poolEntry?1:0)<<','<<onlineGeneration;
      poolFile<<','<<episodeSteps<<','<<fallbackReason<<','<<episodeStartSteps<<','<<completed.load()<<','<<sourceTile<<','<<player.archive_source_update_count()<<'\n';
      if(!poolFile)throw std::runtime_error("pool provenance CSV write failed");
    }
  }
 } catch(const std::exception& e) {
   workerFailed.store(true);std::lock_guard<std::mutex> lock(outputMutex);
   std::cerr<<"worker "<<id<<" failed: "<<e.what()<<'\n';
 }
}

bool milestone(long long requested) {
  for(long long x:{50000000000LL,100000000000LL,200000000000LL})
    if(requested==x)return true;
  return false;
}
std::string step_label(long long steps) {
  if(steps%1000000000LL==0)return std::to_string(steps/1000000000LL)+"B";
  if(steps%1000000LL==0)return std::to_string(steps/1000000LL)+"M";
  if(steps%1000LL==0)return std::to_string(steps/1000LL)+"K";
  return std::to_string(steps);
}
std::string checkpoint_kind(long long requested,bool final) {
  std::string kind;
  auto add=[&](const char* name){if(!kind.empty())kind+='-';kind+=name;};
  if(requested==cfg.first)add("first");
  if(cfg.extra_checkpoint&&requested==cfg.extra_checkpoint)add("extra");
  if(milestone(requested))add("milestone");
  if(final)add("final");
  if(requested%cfg.period==0)add("periodic");
  if(kind.empty())add("tc-reset");
  return kind;
}
std::string checkpoint_name(unsigned long long ordinal,long long requested,long long actual,const std::string& kind) {
  std::ostringstream name;
  name<<cfg.run_name()<<"__every_"<<step_label(cfg.period)<<"__save_"<<std::setw(6)<<std::setfill('0')<<ordinal
      <<"__requested_"<<step_label(requested)<<"__actual_"<<actual<<"__"<<kind;
  return name.str();
}
// Read our generated positive integer metadata without deriving state from names.
bool metadata_integer(const std::string& text,const std::string& key,unsigned long long& value) {
  const std::string prefix="\""+key+"\":";
  const auto start=text.find(prefix);if(start==std::string::npos)return false;
  const auto begin=start+prefix.size();const auto end=text.find_first_not_of("0123456789",begin);
  if(end==begin||end==std::string::npos||(text[end]!=','&&text[end]!='}'))return false;
  try {value=std::stoull(text.substr(begin,end-begin));return true;}catch(...){return false;}
}
void save_checkpoint(long long requested,bool final,bool manual=false) {
  exploration_io::Guard io(cfg.checkpoint_io_lock,cfg.checkpoint_io_timeout_seconds);
  std::cout<<"checkpoint_io_wait,operation,save,seconds,"<<io.wait_seconds()<<std::endl;
  const long long actual=completed.load();
  experienceBudget.assert_quiescent();
  const auto balance=experienceBudget.snapshot();
  if(balance.normal_steps+balance.archive_steps!=static_cast<unsigned long long>(actual))
    throw std::runtime_error("experience allocation does not match global steps");
  allocation_snapshot(-1);
  if(balance.normal_completed+balance.archive_completed!=episodes.load())throw std::runtime_error("experience episode counter mismatch");
  if(checkpointOrdinal==std::numeric_limits<unsigned long long>::max())throw std::runtime_error("checkpoint ordinal exhausted");
  const auto ordinal=checkpointOrdinal+1;
  const std::string kind=manual?"manual":checkpoint_kind(requested,final);
  const std::string name=checkpoint_name(ordinal,requested,actual,kind);
  const fs::path root=cfg.output/"checkpoints",dest=root/name,tmp=root/(name+".tmp-"+std::to_string(getpid()));
  fs::create_directories(root);
  if(fs::exists(dest)||!fs::create_directory(tmp))throw std::runtime_error("checkpoint path collision");
  summaryFile.flush();episodeFile.flush();sampleFile.flush();allocationFile.flush();
  if(!allocationFile)throw std::runtime_error("allocation CSV flush failed");
  availabilityFile.flush();if(!availabilityFile)throw std::runtime_error("availability CSV flush failed");
  if(cfg.start_pool_per_million)poolFile.flush();
  if(!summaryFile||!episodeFile||!sampleFile||(cfg.start_pool_per_million&&!poolFile))throw std::runtime_error("CSV flush failed");
  if(!save_model(tmp/"model.bin"))throw std::runtime_error("full model save failed");
  sync_path(tmp/"model.bin");
  std::ofstream state(tmp/"driver.txt");
  state<<std::setprecision(17)<<"EXPLORATION_DRIVER 4\n"<<std::quoted(cfg.identity())<<'\n'
       <<binaryHash<<'\n'<<actual<<' '<<episodes.load()<<' '<<resetEpoch<<' '<<nextPeriodic<<' '
       <<ordinal<<' '<<priorSeconds+std::chrono::duration<double>(Clock::now()-started).count()<<'\n';
  experienceBudget.save(state);
  if(cfg.archive_source_episodes)onlineArchive.save(state);
  for(const auto& p:workers) {
    const Worker& w=*p;
    state<<w.episode<<' '<<w.localActions<<' '<<w.localSteps<<' '<<w.windowSteps<<'\n'
      <<w.environment<<'\n'<<w.explore<<'\n'<<w.diagnostic<<'\n';
    w.total.save(state);w.window.save(state);
  }
  checked_close(state,"driver checkpoint");sync_path(tmp/"driver.txt");
  std::ofstream meta(tmp/"checkpoint.json");
  meta<<"{\"format_version\":3,\"experiment_id\":"<<json_string(cfg.experiment_id)
      <<",\"run_name\":"<<json_string(cfg.run_name())<<",\"seed\":"<<cfg.seed
      <<",\"checkpoint_interval_steps\":"<<cfg.period<<",\"save_ordinal\":"<<ordinal
      <<",\"kind\":"<<json_string(kind)<<",\"first\":"<<(requested==cfg.first?"true":"false")
      <<",\"periodic\":"<<(requested%cfg.period==0?"true":"false")
      <<",\"requested_steps\":"<<requested<<",\"actual_steps\":"<<actual
      <<",\"milestone\":"<<(milestone(requested)?"true":"false")
      <<",\"final\":"<<(final?"true":"false")<<",\"source_sha256\":"<<json_string(cfg.source)
      <<",\"checkpoint_format\":"<<json_string(cfg.checkpoint_format)
      <<",\"model_access\":"<<json_string(cfg.model_access)
      <<",\"binary_sha256\":"<<json_string(binaryHash)<<"}\n";
  checked_close(meta,"checkpoint metadata");sync_path(tmp/"checkpoint.json");
  std::ofstream commit(tmp/"COMPLETE");
  commit<<"EXPLORATION_FULL_V2 "<<sha256_file(tmp/"driver.txt")<<' '<<sha256_file(tmp/"model.bin")<<'\n';
  checked_close(commit,"commit marker");sync_path(tmp/"COMPLETE");sync_path(tmp,true);
  fs::rename(tmp,dest);sync_path(root,true);
  const fs::path latestTmp=root/("LATEST.tmp-"+std::to_string(getpid()));
  {std::ofstream latest(latestTmp);latest<<name<<'\n';checked_close(latest,"latest pointer");}
  sync_path(latestTmp);fs::rename(latestTmp,root/"LATEST");sync_path(root,true);
  checkpointOrdinal=ordinal;
  statusFile<<ordinal<<','<<kind<<','<<cfg.run_name()<<','<<cfg.period<<','<<requested<<','<<actual<<','<<name<<','<<(milestone(requested)?1:0)<<','<<final<<'\n';statusFile.flush();
  if(!statusFile)throw std::runtime_error("checkpoint status write failed");
  std::cout<<"checkpoint_saved,requested,"<<requested<<",actual,"<<actual<<",path,"<<dest<<'\n';std::cout.flush();
  // Only directories created by this run and bearing our exact marker are
  // eligible. Milestones and final checkpoints are never deleted.
  if(cfg.retain) {
    std::vector<std::pair<unsigned long long,fs::path>> rolling;
    for(const auto& entry:fs::directory_iterator(root)) {
      const fs::path p=entry.path();
      if(!entry.is_directory()||!fs::is_regular_file(p/"COMPLETE")||!fs::is_regular_file(p/"checkpoint.json"))continue;
      std::ifstream marker(p/"COMPLETE");std::string markerMagic;marker>>markerMagic;
      if(markerMagic!="EXPLORATION_FULL_V2")continue;
      std::ifstream metadata(p/"checkpoint.json");std::string text((std::istreambuf_iterator<char>(metadata)),{});
      if(text.find("\"source_sha256\":"+json_string(cfg.source))==std::string::npos||
         text.find("\"milestone\":false")==std::string::npos||text.find("\"final\":false")==std::string::npos)continue;
      if(text.find("\"format_version\":3,")==std::string::npos||
         text.find("\"run_name\":"+json_string(cfg.run_name())+",")==std::string::npos||
         text.find("\"binary_sha256\":"+json_string(binaryHash))==std::string::npos)continue;
      unsigned long long savedOrdinal=0,savedActual=0,savedPeriod=0;
      if(!metadata_integer(text,"save_ordinal",savedOrdinal)||!metadata_integer(text,"actual_steps",savedActual)||
         !metadata_integer(text,"checkpoint_interval_steps",savedPeriod)||savedOrdinal==0||savedActual>static_cast<unsigned long long>(actual)||
         savedOrdinal>ordinal||savedPeriod!=static_cast<unsigned long long>(cfg.period))continue;
      rolling.emplace_back(savedOrdinal,p);
    }
    std::sort(rolling.begin(),rolling.end());
    while(rolling.size()>2) {fs::remove_all(rolling.front().second);rolling.erase(rolling.begin());}
  }
  io.release();
}

void load_checkpoint() {
  exploration_io::Guard io(cfg.checkpoint_io_lock,cfg.checkpoint_io_timeout_seconds);
  std::cout<<"checkpoint_io_wait,operation,restore,seconds,"<<io.wait_seconds()<<std::endl;
  std::ifstream commit(cfg.resume/"COMPLETE");std::string magic,driverSha,modelSha;
  if(!(commit>>magic>>driverSha>>modelSha)||magic!="EXPLORATION_FULL_V2")throw std::runtime_error("incomplete or incompatible checkpoint");
  if(sha256_file(cfg.resume/"driver.txt")!=driverSha||sha256_file(cfg.resume/"model.bin")!=modelSha)
    throw std::runtime_error("checkpoint checksum mismatch");
  std::ifstream state(cfg.resume/"driver.txt");int version;std::string identity,savedBinary;
  state>>magic>>version>>std::quoted(identity)>>savedBinary;
  if(magic!="EXPLORATION_DRIVER"||version!=4||identity!=cfg.identity()||savedBinary!=binaryHash)
    throw std::runtime_error("resume source/binary/config mismatch");
  long long steps=-1;unsigned long long count=0;
  state>>steps>>count>>resetEpoch>>nextPeriodic>>checkpointOrdinal>>priorSeconds;
  if(!state||steps<0||steps>=cfg.steps||resetEpoch<0||nextPeriodic<=0||checkpointOrdinal==0)throw std::runtime_error("invalid saved step/schedule");
  completed.store(steps);episodes.store(count);
  experienceBudget.load(state);
  const auto balance=experienceBudget.snapshot();
  if(balance.normal_steps+balance.archive_steps!=static_cast<unsigned long long>(steps)||balance.normal_completed+balance.archive_completed!=count)throw std::runtime_error("restored experience allocation mismatch");
  if(cfg.archive_source_episodes)onlineArchive.load(state);
  unsigned long long sumEpisodes=0;long long sumSteps=0;
  for(auto& p:workers) {
    Worker& w=*p;state>>w.episode>>w.localActions>>w.localSteps>>w.windowSteps;
    state>>w.environment>>w.explore>>w.diagnostic;w.total.load(state);w.window.load(state);
    sumEpisodes+=w.episode;sumSteps+=w.localSteps;
  }
  state>>std::ws;
  if(!state.eof()||sumEpisodes!=count||sumSteps!=steps)throw std::runtime_error("invalid saved worker counters");
  if(!load_model(cfg.resume/"model.bin"))throw std::runtime_error("model/optimizer restore failed");
  io.release();
}

long long integer(const std::string& s,bool zero=false) {
  size_t used=0;long long x=std::stoll(s,&used);
  if(used!=s.size()||x<(zero?0:1))throw std::runtime_error("invalid nonnegative integer: "+s);
  return x;
}
void parse(int argc,char** argv) {
  if(argc<2)throw std::runtime_error("usage: playgame_VSE_exploration SEED --output-dir DIR --behavior greedy|tc-ucb [--experiment-id SAFE_NAME] [--steps N] [--resume-full DIR] [--formula paper|standard|source] [--ucb-c C] [--warmup-steps N] [--decay-end-steps N] [--threads N] [--model-access serialized|async] [--checkpoint-format dense|compact] [--feature-cache-mib 0|16|32] [--archive-mode random128|legacy] [--archive-condition B|Bprime|Bdoubleprime] [--archive-start-policy requests|steps] [--archive-source-update 0|1]");
  long long seed=integer(argv[1],true);if(seed>std::numeric_limits<int>::max())throw std::runtime_error("seed too large");cfg.seed=seed;
  bool formulaExplicit=false,archiveModeExplicit=false,archiveEpisodesExplicit=false;
  bool probabilityExplicit=false,policyExplicit=false,sourceUpdateExplicit=false;
  for(int i=2;i<argc;i+=2) {
    if(i+1>=argc)throw std::runtime_error("option requires value");
    std::string key=argv[i],value=argv[i+1];
    if(key=="--experiment-id")cfg.experiment_id=value;
    else if(key=="--output-dir")cfg.output=value;
    else if(key=="--resume-full")cfg.resume=value;
    else if(key=="--start-pool")cfg.start_pool_path=value;
    else if(key=="--start-pool-sha256")cfg.start_pool_sha256=value;
    else if(key=="--start-pool-manifest")cfg.start_pool_manifest=value;
    else if(key=="--start-pool-manifest-sha256")cfg.start_pool_manifest_sha256=value;
    else if(key=="--start-pool-per-million") {
      const auto n=integer(value,true);
      if(n>=1000000)throw std::runtime_error("pool proportion must be between 0 and 999999 per million");
      cfg.start_pool_per_million=static_cast<int>(n);probabilityExplicit=true;
    }
    else if(key=="--checkpoint-io-lock")cfg.checkpoint_io_lock=value;
    else if(key=="--stop-after-checkpoint-file")cfg.stop_after_checkpoint_file=value;
    else if(key=="--save-and-stop-file")cfg.save_and_stop_file=value;
    else if(key=="--checkpoint-io-timeout-seconds") {
      cfg.checkpoint_io_timeout_seconds=integer(value);
      if(cfg.checkpoint_io_timeout_seconds>86400)throw std::runtime_error("IO wait timeout exceeds one day");
    }
    else if(key=="--steps")cfg.steps=integer(value);
    else if(key=="--threads"){long long n=integer(value);if(n>1024)throw std::runtime_error("too many threads");cfg.threads=n;}
    else if(key=="--first-checkpoint-step")cfg.first=integer(value);
    else if(key=="--extra-checkpoint-step")cfg.extra_checkpoint=integer(value,true);
    else if(key=="--checkpoint-interval")cfg.period=integer(value);
    else if(key=="--archive-source-episodes"){const auto n=integer(value,true);if(n!=0&&n!=128)throw std::runtime_error("archive source episodes must be0or128");cfg.archive_source_episodes=static_cast<int>(n);archiveEpisodesExplicit=true;}
    else if(key=="--archive-mode"){cfg.archive_mode=value;archiveModeExplicit=true;}
    else if(key=="--archive-condition")cfg.archive_condition=value;
    else if(key=="--archive-start-policy"){cfg.archive_start_policy=value;policyExplicit=true;}
    else if(key=="--archive-source-update"){const auto n=integer(value,true);if(n>1)throw std::runtime_error("archive source update must be0or1");cfg.archive_source_update=n;sourceUpdateExplicit=true;}
    else if(key=="--archive-promote32-percent"||key=="--archive-promote64-percent"){const auto n=integer(value,true);if(n>99)throw std::runtime_error("archive percentage must be0..99");if(key=="--archive-promote32-percent")cfg.archive_promote32_percent=n;else cfg.archive_promote64_percent=n;}
    else if(key=="--summary-interval")cfg.summary=integer(value);
    else if(key=="--sample-every")cfg.sample_every=integer(value,true);
    else if(key=="--behavior")cfg.behavior=value;
    else if(key=="--model-access")cfg.model_access=value;
    else if(key=="--checkpoint-format")cfg.checkpoint_format=value;
    else if(key=="--feature-cache-mib") {
      const auto mib=integer(value,true);
      if(mib!=0&&mib!=16&&mib!=32)throw std::runtime_error("feature cache must be 0|16|32 MiB per worker");
      cfg.feature_cache_mib=static_cast<int>(mib);
    }
    else if(key=="--ucb-formula"||key=="--formula"){cfg.formula=value;formulaExplicit=true;}
    else if(key=="--ucb-c"){size_t n=0;cfg.c=std::stod(value,&n);if(n!=value.size()||!std::isfinite(cfg.c)||cfg.c<0)throw std::runtime_error("invalid UCB coefficient");}
    else if(key=="--warmup-steps")cfg.warmup=integer(value,true);
    else if(key=="--decay-end-steps")cfg.decay=integer(value,true);
    else if(key=="--retention"){if(value!="yes"&&value!="no")throw std::runtime_error("retention requires yes|no");cfg.retain=value=="yes";}
    else throw std::runtime_error("unknown option: "+key);
  }
  if(cfg.archive_promote32_percent>cfg.archive_promote64_percent)throw std::runtime_error("archive32 promotion percentage must not exceed archive64 percentage");
  if(cfg.archive_mode!="random128"&&cfg.archive_mode!="legacy")throw std::runtime_error("archive mode must be random128|legacy");
  if(cfg.archive_condition!="custom") {
    if(cfg.archive_condition!="B"&&cfg.archive_condition!="Bprime"&&cfg.archive_condition!="Bdoubleprime")throw std::runtime_error("archive condition must be B|Bprime|Bdoubleprime");
    const std::string policy=cfg.archive_condition=="Bdoubleprime"?"steps":"requests";
    const bool update=cfg.archive_condition=="Bprime";
    if((probabilityExplicit&&cfg.start_pool_per_million!=500000)||(policyExplicit&&cfg.archive_start_policy!=policy)||(sourceUpdateExplicit&&cfg.archive_source_update!=update))throw std::runtime_error("archive condition conflicts with explicit allocation settings");
    cfg.start_pool_per_million=500000;cfg.archive_start_policy=policy;cfg.archive_source_update=update;
  }
  if(cfg.archive_start_policy!="requests"&&cfg.archive_start_policy!="steps")throw std::runtime_error("archive start policy must be requests|steps");
  if(cfg.archive_start_policy=="steps"&&cfg.start_pool_per_million!=500000)throw std::runtime_error("steps balance requires archive target500000 per million");
  if(cfg.archive_source_update&&!cfg.start_pool_per_million)throw std::runtime_error("source update requires archive starts");
  if(archiveEpisodesExplicit){
    const std::string requested=cfg.archive_source_episodes?"random128":"legacy";
    if(archiveModeExplicit&&cfg.archive_mode!=requested)throw std::runtime_error("conflicting archive-mode and archive-source-episodes");
    cfg.archive_mode=requested;
  }
  if(cfg.start_pool_per_million)cfg.archive_source_episodes=cfg.archive_mode=="random128"?128:0;
  else{
    if(archiveEpisodesExplicit&&cfg.archive_source_episodes)throw std::runtime_error("archive source episodes require nonzero mixed-start proportion");
    cfg.archive_source_episodes=0;cfg.archive_mode="disabled";
  }
  if(cfg.experiment_id.empty()||cfg.experiment_id.size()>64||
     cfg.experiment_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
    throw std::runtime_error("experiment ID must be 1..64 ASCII letters, digits, underscore or hyphen");
  if(cfg.behavior!="greedy"&&cfg.behavior!="tc-ucb")throw std::runtime_error("invalid behavior");
  if(cfg.model_access!="serialized"&&cfg.model_access!="async")throw std::runtime_error("model access must be serialized|async");
  if(cfg.checkpoint_format!="dense"&&cfg.checkpoint_format!="compact")throw std::runtime_error("checkpoint format must be dense|compact");
#ifdef NT9_AOS_PROBE
  if(cfg.checkpoint_format!="compact")throw std::runtime_error("AoS training supports compact checkpoints only");
#endif
#ifndef NT9_COMPACT_STORAGE
  if(cfg.checkpoint_format=="compact")throw std::runtime_error("compact checkpoint requires NT9 compact storage");
#endif
  if(cfg.formula!="standard"&&cfg.formula!="paper"&&cfg.formula!="source")throw std::runtime_error("invalid UCB formula");
  if(cfg.behavior=="tc-ucb"&&!formulaExplicit)throw std::runtime_error("tc-ucb requires explicit --formula paper|standard|source");
  if(!valid_hash(cfg.source))throw std::runtime_error("build requires valid EXPLORATION_SOURCE_SHA256");
  if(cfg.start_pool_per_million) {
    const bool single=!cfg.start_pool_path.empty()&&valid_hash(cfg.start_pool_sha256)&&cfg.start_pool_manifest.empty()&&cfg.start_pool_manifest_sha256.empty();
    const bool manifest=!cfg.start_pool_manifest.empty()&&valid_hash(cfg.start_pool_manifest_sha256)&&cfg.start_pool_path.empty()&&cfg.start_pool_sha256.empty();
    // Online archives can collect their first source from a normal game.
    const bool self_start=cfg.archive_source_episodes&&cfg.start_pool_path.empty()&&cfg.start_pool_sha256.empty()&&cfg.start_pool_manifest.empty()&&cfg.start_pool_manifest_sha256.empty();
    if(!single&&!manifest&&!self_start)throw std::runtime_error("mixed start requires exactly one pool or manifest and its SHA256, or no pool arguments in random128 mode");
  } else if(!cfg.start_pool_path.empty()||!cfg.start_pool_sha256.empty()||!cfg.start_pool_manifest.empty()||!cfg.start_pool_manifest_sha256.empty())
    throw std::runtime_error("pool path/hash require a nonzero mixed-start proportion");
  if(cfg.output.empty())throw std::runtime_error("invalid output directory");
  if(!cfg.checkpoint_io_lock.empty())cfg.checkpoint_io_lock=fs::absolute(cfg.checkpoint_io_lock).lexically_normal();
  if(!cfg.stop_after_checkpoint_file.empty())cfg.stop_after_checkpoint_file=fs::absolute(cfg.stop_after_checkpoint_file).lexically_normal();
  if(cfg.save_and_stop_file.empty())cfg.save_and_stop_file=cfg.output/"SAVE_AND_STOP.request";
  cfg.save_and_stop_file=fs::absolute(cfg.save_and_stop_file).lexically_normal();
  if(fs::exists(cfg.save_and_stop_file))throw std::runtime_error("save-and-stop request already exists; resume requires a fresh request path");
  if(fs::exists(cfg.output))throw std::runtime_error("output directory already exists; use a fresh directory even on resume");
  exploration::config.mode=cfg.behavior=="greedy"?exploration::Mode::greedy:exploration::Mode::tc_ucb;
  exploration::config.formula=exploration::parse_formula(cfg.formula);
  exploration::config.c=cfg.c;exploration::config.warmup_steps=cfg.warmup;exploration::config.decay_end_steps=cfg.decay;
  exploration::config.teacher=exploration::Teacher::greedy_max;
  exploration::config.validate();
  exploration_concurrency::active_mode=exploration_concurrency::parse_mode(cfg.model_access);
  exploration_candidate_cache::enabled=cfg.feature_cache_mib!=0;
  exploration_candidate_cache::budget_bytes=std::size_t(cfg.feature_cache_mib?cfg.feature_cache_mib:16)*1024u*1024u;
  learning_rule::active_schedule.mode=learning_rule::Mode::tc;
}

int main(int argc,char** argv) {
 try {
  parse(argc,argv);binaryHash=sha256_file("/proc/self/exe");selfTick=save_stop_process_starttick();
  {std::ifstream boot("/proc/sys/kernel/random/boot_id");if(!(boot>>selfBoot))throw std::runtime_error("cannot read boot identity");}
  if(cfg.checkpoint_io_lock.empty())throw std::runtime_error("research driver requires --checkpoint-io-lock shared-NFS-directory");
  init_movetable();
  if(!cfg.start_pool_path.empty()||!cfg.start_pool_manifest.empty()) {
    std::set<std::string> sourceGames;
    auto loadPool=[&](const fs::path& path,const std::string& hash) {
      if(!valid_hash(hash)||sha256_file(path)!=hash)throw std::runtime_error("start pool SHA256 mismatch");
      for(const auto& p:startPools)if(p.sha==hash)throw std::runtime_error("duplicate start pool SHA256");
      NamedPool p;p.sha=hash;p.data.load(path.string());
      if(sha256_file(path)!=hash)throw std::runtime_error("start pool changed while loading");
      for(const auto& e:p.data.entries) {
        const std::string key=p.data.metadata.at("source_run")+'\n'+p.data.metadata.at("source_policy")+'\n'+e.source_game;
        if(!sourceGames.insert(key).second)throw std::runtime_error("source game appears in multiple pools");
      }
      startPools.push_back(std::move(p));
    };
    if(!cfg.start_pool_path.empty())loadPool(cfg.start_pool_path,cfg.start_pool_sha256);
    else {
      if(sha256_file(cfg.start_pool_manifest)!=cfg.start_pool_manifest_sha256)throw std::runtime_error("pool manifest SHA256 mismatch");
      std::ifstream manifest(cfg.start_pool_manifest);std::string line;
      if(!std::getline(manifest,line)||line!="start_pool_manifest_v1")throw std::runtime_error("invalid pool manifest header");
      while(std::getline(manifest,line)) {
        const auto tab=line.find('\t');
        if(tab!=64||line.size()<=65||line.find('\t',tab+1)!=std::string::npos)throw std::runtime_error("invalid pool manifest row");
        fs::path path=line.substr(tab+1);
        if(path.is_relative())path=cfg.start_pool_manifest.parent_path()/path;
        loadPool(path,line.substr(0,tab));
      }
      if(!manifest.eof()||startPools.empty()||sha256_file(cfg.start_pool_manifest)!=cfg.start_pool_manifest_sha256)
        throw std::runtime_error("empty, unreadable, or changed pool manifest");
    }
  }
  if(cfg.archive_source_episodes){
    onlineArchive.configure(cfg.seed,cfg.threads);
    onlineArchive.curriculum.configure_percent(cfg.archive_promote32_percent,cfg.archive_promote64_percent);
    std::set<std::array<int,16>> boards;
    for(const auto& pool:startPools)for(auto e:pool.data.entries){
      e.board_id=pool.sha+"_"+e.board_id;e.source_game=pool.sha+"_"+e.source_game;
      if(boards.insert(e.board).second)onlineArchive.bootstrap.push_back(std::move(e));
    }
  }
  for(int i=0;i<cfg.threads;++i){auto w=std::make_unique<Worker>();w->initialize(i);workers.push_back(std::move(w));}
  experienceBudget.configure(cfg.threads);
  nextPeriodic=cfg.period;
  if(cfg.resume.empty())init_tuple();else load_checkpoint();
  fs::create_directories(cfg.output);
  summaryFile.open(cfg.output/"summary.csv");episodeFile.open(cfg.output/"episodes.csv");
  allocationFile.open(cfg.output/"experience_allocation.csv");
  availabilityFile.open(cfg.output/"archive_availability.csv");
  if(!availabilityFile)throw std::runtime_error("cannot create availability CSV");
  availabilityFile<<"completed_steps,elapsed_seconds,availability,desired_tile_exponent,active_tile_exponent,generation\n";
  if(!allocationFile)throw std::runtime_error("cannot create allocation CSV");
  allocationFile<<"worker,completed_steps,normal_steps,archive_steps,archive_step_ratio,normal_requests,archive_requests,normal_starts,archive_starts,archive_start_ratio,archive_fallbacks,normal_completed,archive_completed,inflight_normal,inflight_archive,start_policy,target_archive_step_ratio,archive_request_probability,source_update_enabled\n";
  sampleFile.open(cfg.output/"decision_samples.csv");statusFile.open(cfg.output/"checkpoint_status.csv");
  if(!summaryFile||!episodeFile||!sampleFile||!statusFile)throw std::runtime_error("cannot create output CSV");
  if(cfg.start_pool_per_million) {
    poolFile.open(cfg.output/"start_pool_episodes.csv");
    if(!poolFile)throw std::runtime_error("cannot create pool provenance CSV");
    poolFile<<"worker,worker_episode,start_kind,pool_sha256,board_id,source_game,source_turn,source_cumulative_score";
    if(cfg.archive_source_episodes)poolFile<<",archive_requested,archive_empty_fallback,archive_generation";
    poolFile<<",episode_training_steps,fallback_reason,start_completed_steps,end_completed_steps,source_tile_exponent,source_updates\n";
  }
  summaryFile<<"worker,completed_steps,worker_steps,window_steps,"<<exploration_observer::Counters::header()<<'\n';
  episodeFile<<"worker,worker_episode,completed_steps,worker_steps,episode_training_steps,original_score,original_max_tile_exponent,original_turn,restart_segments,start_kind,source_turn,source_cumulative_score,total_score,total_turn,archive_requested,fallback_reason,start_completed_steps,source_tile_exponent,source_updates,start_elapsed_seconds,end_elapsed_seconds\n";
  sampleFile<<"worker,worker_episode,restart,turn,worker_action,completed_steps,behavior,stage,progress,action,legal,raw_score,minA,bonus,rank,chosen,collection_greedy,effective_c,score_scale,behavior_score,paper_log_floor_count,paper_denominator_floor,score_scale_floor";
  for(int k=0;k<16;++k)sampleFile<<",board"<<k;
  sampleFile<<'\n';
  statusFile<<"save_ordinal,kind,run_name,checkpoint_interval_steps,requested_steps,actual_steps,checkpoint,milestone,final\n";
  std::ofstream config(cfg.output/"config.json");
  config<<"{\"experiment_id\":"<<json_string(cfg.experiment_id)<<",\"run_name\":"<<json_string(cfg.run_name())<<",\"driver_state_version\":4,\"identity\":"<<json_string(cfg.identity())<<",\"binary_sha256\":"<<json_string(binaryHash)
      <<",\"source_sha256\":"<<json_string(cfg.source)<<",\"target_steps\":"<<cfg.steps
      <<",\"seed\":"<<cfg.seed<<",\"threads\":"<<cfg.threads
      <<",\"model_precision\":"<<json_string(MODEL_PRECISION)
      <<",\"first_checkpoint_step\":"<<cfg.first<<",\"checkpoint_interval\":"<<cfg.period
      <<",\"extra_checkpoint_step\":"<<cfg.extra_checkpoint
      <<",\"summary_interval\":"<<cfg.summary<<",\"sample_every\":"<<cfg.sample_every
      <<",\"retention_enabled\":"<<(cfg.retain?"true":"false")
      <<",\"checkpoint_format\":"<<json_string(cfg.checkpoint_format)
      <<",\"feature_cache_mib_per_worker\":"<<cfg.feature_cache_mib
      <<",\"feature_cache_specification\":"<<json_string(exploration_candidate_cache::specification)
      <<",\"behavior\":"<<json_string(cfg.behavior)<<",\"formula\":"<<json_string(cfg.formula)
      <<",\"formula_specification\":"<<json_string(exploration::formula_specification)
      <<",\"sample_semantics\":\"raw_score and collection_greedy are decision-time values; actual greedy-max TD teacher is recomputed during backward update; bonus and behavior_score use normalized score units when c>0\""
      <<",\"sample_board\":\"selectHand input state after spawn (restart continuation uses restored afterstate); board0..board15 row-major tile exponents, 0=empty and k>0=2^k\""
      <<",\"guard_counts\":\"paper_log_floor_actions counts legal candidate actions with minA<1; paper_denominator_floor_decisions counts decisions with sum(minA)<1e-12; score_scale_floor_decisions counts decisions with normalization denominator floored to1; guards count only active c>0 calculations\""
      <<",\"ucb_c\":"<<std::setprecision(17)<<cfg.c<<",\"warmup_steps\":"<<cfg.warmup
      <<",\"decay_end_steps\":"<<cfg.decay<<",\"teacher\":\"greedy-max\",\"tc_rule\":\"legacy TC 0.5 post-update coherence\""
      <<",\"resume_from\":"<<json_string(cfg.resume.string())
      <<",\"archive_condition\":"<<json_string(cfg.archive_condition)
      <<",\"archive_start_policy\":"<<json_string(cfg.archive_start_policy)
      <<",\"archive_source_update\":"<<(cfg.archive_source_update?"true":"false")
      <<",\"experience_steps_semantics\":\"initial+all restart learning-step units; actual origin, normal fallback counted normal; terminal units retained; extra source updates separately counted and not added to existing global schedule\""
      <<",\"archive_step_target\":"<<(cfg.archive_start_policy=="steps"?"0.5":"null")
      <<",\"archive_request_probability\":"<<(cfg.archive_start_policy=="requests"?std::to_string(double(cfg.start_pool_per_million)/1000000.0):"null")
      <<",\"archive_mode\":"<<json_string(cfg.archive_mode)
      <<",\"archive_source_episodes\":"<<cfg.archive_source_episodes
      <<",\"archive_promote32_percent\":"<<cfg.archive_promote32_percent
      <<",\"archive_promote64_percent\":"<<cfg.archive_promote64_percent
      <<",\"archive_gate_window\":1000,\"archive_gate32_min_count\":"<<onlineArchive.curriculum.promote32_count
      <<",\"archive_gate64_min_count\":"<<onlineArchive.curriculum.promote64_count
      <<",\"online_archive_contract\":"<<json_string(cfg.archive_source_episodes?"source afterstate used128 times, independent actual spawn9:1 each start, results may repeat; use=original+restart episode; complete all128 claims before next source; normal-original first16k/32k/64k sources; normal32k count on complete1000window: promote32>=configuredPercent, promote64>configuredPercent; monotonic; source-turn inherited; identical last/pending source boards rejected; desired64 with no64source uses a different new32source; otherwise source starvation and busy batch fall back normal":(cfg.start_pool_per_million?"legacy fixed immutable afterstate pool; no online capture/curriculum":"disabled; normal starts only"))
      <<",\"start_pool_sha256\":"<<json_string(cfg.start_pool_sha256)
      <<",\"start_pool_per_million\":"<<cfg.start_pool_per_million
      <<",\"start_pool_path\":"<<json_string(cfg.start_pool_path.string())
      <<",\"start_pool_manifest_sha256\":"<<json_string(cfg.start_pool_manifest_sha256)
      <<",\"start_pool_manifest\":"<<json_string(cfg.start_pool_manifest.string())
      <<",\"start_pool_sampling\":"<<json_string(cfg.archive_start_policy=="steps"?"request origin with fewer completed initial+restart learning steps; target archive fraction0.5; ties use in-flight counts then saved cursor; missing source falls back normal":cfg.archive_source_episodes?"archive request per configured probability;128 independent with-replacement uses per source; each initial spawn9:1 and empty-cell uniform, exactly once":(cfg.start_pool_per_million?"legacy uniform pool then uniform board; pool probability specified per episode":"normal starts only"))
      <<",\"start_pool_score_semantics\":\"original_score is continuation reward; total_score=source_cumulative_score+original_score; ordinary starts use source score 0; donor rewards are never added to learning updates\""
      <<",\"episode_turn_semantics\":\"original_turn remains local continuation turn (includes terminal turn); total_turn=source_turn+original_turn; saved afterstate source_turn counts completed actions; decision_samples turn uses source_turn+local turn; restart/history/training-step counters remain local\""
      <<",\"checkpoint_io_lock\":"<<json_string(cfg.checkpoint_io_lock.string())
      <<",\"checkpoint_io_timeout_seconds\":"<<cfg.checkpoint_io_timeout_seconds
      <<",\"stop_after_checkpoint_file\":"<<json_string(cfg.stop_after_checkpoint_file.string())
      <<",\"save_and_stop_file\":"<<json_string(cfg.save_and_stop_file.string())
      <<",\"save_and_stop_capability\":\"manual_checkpoint_stop_v1\""
      <<",\"process_pid\":"<<getpid()<<",\"process_starttick\":"<<selfTick<<",\"process_uid\":"<<getuid()
      <<",\"process_boot_id\":"<<json_string(selfBoot)
      <<",\"model_access\":"<<json_string(cfg.model_access)<<",\"tc_reset_steps\":"<<TC_RESET_STEPS
      <<",\"model_access_contract\":"<<json_string(cfg.model_access=="serialized"?
          "common mutex around selectHand and complete gameEnd backward sweep":
          "atomic model reads and striped per-feature updates; asynchronous values are not a multi-feature snapshot")
      <<",\"rng\":\"seed_seq(run_seed,worker_id,purpose_tag); environment=454e5601 exploration=45585002 diagnostic=44494103\""
      <<",\"placement\":\"top3 ranked by exponent then cell index; original segments only; before action at absolute turn7200/14500 (source_turn+local turn); starts beyond a point do not observe it; outer row0/3 or column0/3 vs corner2x2; dihedral invariant; tied exponents use cell-index order\""
      <<",\"summary_windows\":\"worker-local segment boundaries; counters include actual observed actions; training steps include terminal turns\""
      <<",\"reproducibility\":\"full RNG+optimizer resume; bitwise continuation is tested for one worker only\"}\n";
  checked_close(config,"run config");
  sync_path(cfg.output/"config.json");
  if(cfg.archive_source_episodes){
    onlineArchive.publish=[](random_source_archive::Archive& a,long long step){
      const fs::path dir=cfg.output/"online_archive_epochs";fs::create_directories(dir);
      const fs::path path=dir/("epoch_"+std::to_string(a.generation)+".csv"),tmp=path.string()+".tmp";
      std::ofstream out(tmp);
      out<<"# random_source128_epoch_v1\n# generation="<<a.generation<<"\n# active_target="<<a.active_target<<"\n";
      out<<"board_id,source_game,source_turn,cumulative_score";for(int c=0;c<16;++c)out<<",c"<<c;out<<'\n';
      for(const auto& e:a.active){out<<e.board_id<<','<<e.source_game<<','<<e.source_turn<<','<<e.cumulative_score;for(int c:e.board)out<<','<<c;out<<'\n';}
      checked_close(out,"online archive epoch");sync_path(tmp);fs::rename(tmp,path);sync_path(dir,true);
      if(a.source){
        const fs::path sourcePath=dir/("epoch_"+std::to_string(a.generation)+"_source.txt");std::ofstream cells(sourcePath);
        for(int i=0;i<16;++i)cells<<(i?" ":"")<<a.source->entry.board[i];cells<<'\n';checked_close(cells,"archive source board");sync_path(sourcePath);a.source_board_sha=sha256_file(sourcePath);
      }
      const auto hash=sha256_file(path);
      std::ofstream audit(cfg.output/"online_archive_updates.csv",std::ios::app);
      audit<<step<<','<<a.generation<<','<<a.active.size()<<','<<std::count_if(a.curriculum.next_sources.begin(),a.curriculum.next_sources.end(),[](const auto& x){return bool(x);})<<','<<a.captured<<','<<a.rejected<<','<<a.duplicates<<','<<a.evicted<<','<<a.fallbacks<<','<<a.first_capture_step<<','<<a.active_target<<','<<a.curriculum.desired_exponent<<','<<a.curriculum.window.size()<<','<<a.curriculum.count32<<','<<a.curriculum.count64<<','<<a.starvation<<','<<a.busy_fallbacks<<','<<a.source_board_sha<<','<<(a.source?a.source->entry.source_turn:0)<<','<<std::count(a.state.begin(),a.state.end(),2)<<','<<a.curriculum.promote32_count<<','<<a.curriculum.promote64_count<<','<<bool(a.curriculum.next_sources[0])<<','<<bool(a.curriculum.next_sources[1])<<','<<bool(a.curriculum.next_sources[2])<<','<<(a.curriculum.desired_exponent==16&&a.active_target==15?"fallback_new32_for64":"desired_stage")<<','<<hash<<'\n';
      checked_close(audit,"online archive update audit");sync_path(cfg.output/"online_archive_updates.csv");
      std::cout<<"online_archive_update,step,"<<step<<",generation,"<<a.generation<<",boards,"<<a.active.size()<<",sha256,"<<hash<<std::endl;
      return hash;
    };
    std::ofstream header(cfg.output/"online_archive_updates.csv");header<<"step,generation,active,pending,captured,rejected,duplicates,evicted,fallbacks,first_capture_step,active_target,desired_target,normal_window,count32,count64,source_starvation,batch_busy_fallbacks,source_board_sha256,source_turn,completed_uses,gate32_min_count,gate64_min_count,pending16,pending32,pending64,source_selection,sha256\n";checked_close(header,"online archive audit header");
    const auto hash=onlineArchive.publish(onlineArchive,completed.load());
    if(!cfg.resume.empty()&&hash!=onlineArchive.digest)throw std::runtime_error("restored online archive snapshot checksum mismatch");
    onlineArchive.digest=hash;
    onlineArchive.availability_changed=[](const random_source_archive::Archive& a,long long /*step*/,const std::string& status) {
      availabilityFile<<completed.load()<<','<<std::setprecision(17)<<priorSeconds+std::chrono::duration<double>(Clock::now()-started).count()<<','<<status<<','<<a.curriculum.desired_exponent<<','<<a.active_target<<','<<a.generation<<'\n';
      if(!availabilityFile)throw std::runtime_error("archive availability CSV write failed");
    };
  }
  std::jthread requestMonitor([](std::stop_token stop){
    try{while(!stop.stop_requested()&&!saveStopRequested.load()){
      poll_save_stop();if(saveStopRequested.load())break;
      for(int i=0;i<10&&!stop.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }}catch(const std::exception& e){std::cerr<<"save-and-stop monitor failed: "<<e.what()<<std::endl;workerFailed.store(true);}
  });
  started=Clock::now();
  if(cfg.archive_source_episodes){std::lock_guard<std::mutex> lock(onlineArchive.mutex);onlineArchive.notify_availability(completed.load());}
  std::cout<<"research_exploration,source,"<<cfg.source<<",binary,"<<binaryHash<<",config,"<<cfg.identity()<<'\n';
  bool paused=false,savedInThisOutput=false;
  while(completed.load()<cfg.steps) {
    poll_save_stop();
    if(workerFailed.load())throw std::runtime_error("control monitor failure");
    if(saveStopRequested.load()){
      // No reset/update has occurred since the last committed save. If a
      // checkpoint already represents this model, do not save it twice.
      if(!savedInThisOutput){save_checkpoint(completed.load(),false,true);savedInThisOutput=true;}
      paused=true;break;
    }
    const long long before=completed.load();
    // A saved boundary defers TC reset until immediately before the next
    // training wave; stopping exactly there does not modify the saved model.
    const long long epoch=before/TC_RESET_STEPS;
    if(epoch>resetEpoch){resetErrs();resetEpoch=epoch;std::cout<<"tc_reset,absolute_completed_steps,"<<before<<",epoch,"<<epoch<<'\n';}
    while(nextPeriodic<=before)nextPeriodic+=cfg.period;
    long long requested=std::min(cfg.steps,nextPeriodic);
    if(before<cfg.first)requested=std::min(requested,cfg.first);
    if(cfg.extra_checkpoint&&before<cfg.extra_checkpoint)requested=std::min(requested,cfg.extra_checkpoint);
    for(long long mark:{50000000000LL,100000000000LL,200000000000LL})
      if(mark>before)requested=std::min(requested,mark);
    const long long resetBoundary=(before/TC_RESET_STEPS+1)*TC_RESET_STEPS;
    requested=std::min(requested,resetBoundary);waveTarget=requested;
    exploration::prepare_model_access();
    const auto waveStart=Clock::now();std::vector<std::thread> threads;
    for(int i=0;i<cfg.threads;++i)threads.emplace_back(run_worker,i);
    for(auto& t:threads)t.join();
    if(workerFailed.load())throw std::runtime_error("worker failure; no checkpoint committed");
    const double seconds=std::chrono::duration<double>(Clock::now()-waveStart).count();
    std::cout<<"wave_complete,requested,"<<requested<<",actual,"<<completed.load()<<",compute_seconds,"<<seconds
      <<",steps_per_sec,"<<(completed.load()-before)/seconds<<'\n';
    while(nextPeriodic<=completed.load())nextPeriodic+=cfg.period;
    const bool manual=saveStopRequested.load()&&completed.load()<requested;
    save_checkpoint(manual?completed.load():requested,completed.load()>=cfg.steps,manual);savedInThisOutput=true;
    poll_save_stop();
    if(workerFailed.load())throw std::runtime_error("control monitor failure");
    if(saveStopRequested.load()&&completed.load()<cfg.steps){paused=true;break;}
    if(completed.load()<cfg.steps&&!cfg.stop_after_checkpoint_file.empty()&&fs::exists(cfg.stop_after_checkpoint_file)) {
      paused=true;break;
    }
  }
  // Flush final partial windows after saving so checkpoint counters describe
  // the continuation state rather than a reporting-only reset.
  for(int i=0;i<cfg.threads;++i)flush_window(i,true);
  const double seconds=priorSeconds+std::chrono::duration<double>(Clock::now()-started).count();
  checked_close(availabilityFile,"availability CSV");sync_path(cfg.output/"archive_availability.csv");
  checked_close(allocationFile,"allocation CSV");sync_path(cfg.output/"experience_allocation.csv");
  checked_close(summaryFile,"summary CSV");checked_close(episodeFile,"episode CSV");
  checked_close(sampleFile,"decision CSV");checked_close(statusFile,"checkpoint status CSV");
  if(cfg.start_pool_per_million) {checked_close(poolFile,"pool provenance CSV");sync_path(cfg.output/"start_pool_episodes.csv");}
  for(const char* name:{"summary.csv","episodes.csv","decision_samples.csv","checkpoint_status.csv"})sync_path(cfg.output/name);
  const std::string marker=paused?"RUN_PAUSED.json":"RUN_COMPLETE.json";
  const fs::path doneTmp=cfg.output/(marker+".tmp");
  std::ofstream done(doneTmp);
  done<<"{\"status\":"<<json_string(paused?"paused":"complete")<<",\"requested_steps\":"<<cfg.steps
      <<",\"save_and_stop_requested\":"<<(saveStopRequested.load()?"true":"false")
      <<",\"save_and_stop_nonce\":"<<json_string(saveStopNonce)
      <<",\"save_and_stop_file\":"<<json_string(cfg.save_and_stop_file.string())
      <<",\"actual_steps\":"<<completed.load()<<",\"episodes\":"<<episodes.load()
      <<",\"binary_sha256\":"<<json_string(binaryHash)<<",\"source_sha256\":"<<json_string(cfg.source)
      <<",\"save_ordinal\":"<<checkpointOrdinal<<",\"latest_pointer\":"<<json_string((cfg.output/"checkpoints/LATEST").string())<<"}\n";
  checked_close(done,"run completion marker");sync_path(doneTmp);
  fs::rename(doneTmp,cfg.output/marker);sync_path(cfg.output,true);
  std::cout<<"final_summary,steps,"<<completed.load()<<",episodes,"<<episodes.load()<<",elapsed_seconds,"<<seconds<<'\n';
  return 0;
 } catch(const std::exception& e){std::cerr<<"exploration learner failed: "<<e.what()<<'\n';return 1;}
}
