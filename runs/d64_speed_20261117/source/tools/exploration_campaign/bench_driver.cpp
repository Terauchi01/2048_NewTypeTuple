// Diagnostic harness: reuses the research worker/RNG/telemetry implementation,
// but never creates a resumable research checkpoint or campaign completion.
// An explicitly requested native model-only save is an IO/RAM diagnostic.
#define main exploration_research_main
#include "../../learn_double/playgame_VSE_exploration.cpp"
#undef main
#include <sys/resource.h>
#include "nt9_compact_layout.h"

static double epoch_seconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
static double monotonic_seconds() {
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
static unsigned long long process_starttick() {
  std::ifstream f("/proc/self/stat");std::string line,part;std::getline(f,line);
  const auto end=line.rfind(')');
  if(end==std::string::npos)throw std::runtime_error("cannot read process starttick");
  std::istringstream fields(line.substr(end+2));
  for(int i=0;i<20;++i)if(!(fields>>part))throw std::runtime_error("truncated process stat");
  return std::stoull(part);
}
static unsigned long long current_rss_bytes() {
  std::ifstream f("/proc/self/statm");unsigned long long pages,resident;
  if(!(f>>pages>>resident))throw std::runtime_error("cannot read process RSS");
  const long pageSize=sysconf(_SC_PAGESIZE);
  if(pageSize<=0)throw std::runtime_error("invalid page size");
  return resident*static_cast<unsigned long long>(pageSize);
}
static void atomic_json(const fs::path& path,const std::string& text) {
  const fs::path tmp=path.string()+".tmp-"+std::to_string(getpid());
  std::ofstream f(tmp);f<<text;checked_close(f,"diagnostic JSON");sync_path(tmp);
  fs::rename(tmp,path);sync_path(path.parent_path(),true);
}
static double wait_for_file(const fs::path& file,int seconds,const char* purpose) {
  const auto begin=Clock::now();
  while(!fs::is_regular_file(file)) {
    if(std::chrono::duration<double>(Clock::now()-begin).count()>=seconds)
      throw std::runtime_error(std::string(purpose)+" timeout waiting for "+file.string());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return std::chrono::duration<double>(Clock::now()-begin).count();
}

static double cpu_seconds(const rusage& usage) {
  return double(usage.ru_utime.tv_sec)+usage.ru_utime.tv_usec*1e-6+
         double(usage.ru_stime.tv_sec)+usage.ru_stime.tv_usec*1e-6;
}

int main(int argc,char** argv) {
#ifdef NT9_ADAPTIVE_MUTEX
  std::cout << "adaptive_mutex_fallbacks," << exploration_concurrency::adaptive_fallback_count() << std::endl;
#else
  std::cout << "mutex_mode,baseline" << std::endl;
#endif
 try {
  cfg.steps=1000000;
  cfg.threads=1;
  cfg.sample_every=0;
  fs::path barrier,holdUntil,nativeProbe;
  int barrierTimeout=180;
  if(const char* value=std::getenv("EXPLORATION_BENCH_BARRIER_DIR"))barrier=value;
  std::vector<std::string> arguments{argv[0]};
  for(int i=1;i<argc;++i) {
    const std::string key=argv[i];
    if(key=="--start-barrier"||key=="--barrier-timeout-seconds"||key=="--hold-until"||key=="--save-native-probe") {
      if(i+1>=argc)throw std::runtime_error("benchmark option requires value: "+key);
      const std::string value=argv[++i];
      if(value.empty())throw std::runtime_error("empty benchmark option: "+key);
      if(key=="--start-barrier")barrier=value;
      else if(key=="--hold-until")holdUntil=value;
      else if(key=="--save-native-probe")nativeProbe=value;
      else {const auto parsed=integer(value);if(parsed>3600)throw std::runtime_error("barrier timeout exceeds 3600 seconds");barrierTimeout=parsed;}
    } else arguments.push_back(key);
  }
  std::vector<char*> filtered;
  for(auto& arg:arguments)filtered.push_back(arg.data());
  parse(static_cast<int>(filtered.size()),filtered.data());
  if(!cfg.resume.empty())throw std::runtime_error("benchmark requires a fresh model; resume is disabled");
  if(cfg.steps>20000000)throw std::runtime_error("benchmark target is capped at 20000000 steps");
  if(cfg.threads!=1&&cfg.threads!=2&&cfg.threads!=4&&cfg.threads!=6&&cfg.threads!=8&&cfg.threads!=15&&cfg.threads!=16)
    throw std::runtime_error("benchmark worker count must be 1,2,4,6,8,16");
  if(!barrier.empty()) {
    barrier=fs::absolute(barrier).lexically_normal();fs::create_directories(barrier);
    if(fs::exists(barrier/"GO"))throw std::runtime_error("stale barrier GO already exists");
  }
  if(!holdUntil.empty()) {
    holdUntil=fs::absolute(holdUntil).lexically_normal();
    if(fs::exists(holdUntil))throw std::runtime_error("hold release file already exists");
  }
#ifdef NT9_AOS_PROBE
  if(!nativeProbe.empty())throw std::runtime_error("AoS benchmark refuses native save before allocation");
#endif
  if(!nativeProbe.empty()) {
    nativeProbe=fs::absolute(nativeProbe).lexically_normal();
    if(fs::exists(nativeProbe))throw std::runtime_error("native probe refuses existing destination");
    if(nativeProbe==holdUntil||nativeProbe==barrier/"GO")throw std::runtime_error("native probe conflicts with control file");
#ifndef NT9_COMPACT_STORAGE
    throw std::runtime_error("native probe requires NT9 compact storage");
#endif
  }
  binaryHash=sha256_file("/proc/self/exe");
  const auto initializeStart=Clock::now();
  init_movetable();
  experienceBudget.configure(cfg.threads);
  for(int i=0;i<cfg.threads;++i){auto w=std::make_unique<Worker>();w->initialize(i);workers.push_back(std::move(w));}
  init_tuple();
  exploration::prepare_model_access();
  const double initializeSeconds=std::chrono::duration<double>(Clock::now()-initializeStart).count();
  fs::create_directories(cfg.output);
  summaryFile.open(cfg.output/"summary.csv");episodeFile.open(cfg.output/"episodes.csv");
  allocationFile.open(cfg.output/"experience_allocation.csv");
  if(!allocationFile)throw std::runtime_error("cannot create benchmark allocation CSV");
  allocationFile<<"worker,completed_steps,normal_steps,archive_steps,archive_step_ratio,normal_requests,archive_requests,normal_starts,archive_starts,archive_start_ratio,archive_fallbacks,normal_completed,archive_completed,inflight_normal,inflight_archive,start_policy,target_archive_step_ratio,archive_request_probability,source_update_enabled\n";
  sampleFile.open(cfg.output/"decision_samples.csv");
  if(!summaryFile||!episodeFile||!sampleFile)throw std::runtime_error("cannot create benchmark telemetry");
  summaryFile<<"worker,completed_steps,worker_steps,window_steps,"<<exploration_observer::Counters::header()<<'\n';
  episodeFile<<"worker,worker_episode,completed_steps,worker_steps,episode_training_steps,original_score,original_max_tile_exponent,original_turn,restart_segments,start_kind,source_turn,source_cumulative_score,total_score,total_turn,archive_requested,fallback_reason,start_completed_steps,source_tile_exponent,source_updates,start_elapsed_seconds,end_elapsed_seconds\n";
  sampleFile<<"worker,worker_episode,restart,turn,worker_action,completed_steps,behavior,stage,progress,action,legal,raw_score,minA,bonus,rank,chosen,collection_greedy,effective_c,score_scale,behavior_score,paper_log_floor_count,paper_denominator_floor,score_scale_floor";
  for(int k=0;k<16;++k)sampleFile<<",board"<<k;
  sampleFile<<'\n';
  double barrierWait=0,goObservedEpoch=0;
  if(!barrier.empty()) {
    const fs::path ready=barrier/("ready_"+std::to_string(getpid())+".json");
    if(fs::exists(ready))throw std::runtime_error("barrier ready file already exists");
#ifdef NT9_STORAGE_TEST_DIGITS
    const nt9_compact::Layout layout(NT9_STORAGE_TEST_DIGITS);
#else
    const nt9_compact::Layout layout(9);
#endif
    const auto rss=current_rss_bytes();
    std::ostringstream readyJson;
    readyJson<<std::setprecision(17)<<"{\"pid\":"<<getpid()<<",\"starttick\":"<<process_starttick()
      <<",\"workers\":"<<cfg.threads<<",\"worker_count\":"<<cfg.threads
      <<",\"feature_cache_mib_per_worker\":"<<cfg.feature_cache_mib
      <<",\"source_sha256\":"<<json_string(cfg.source)<<",\"binary_sha256\":"<<json_string(binaryHash)
      <<",\"rss_kib\":"<<rss/1024<<",\"rss_bytes\":"<<rss
      <<",\"model_initialized\":true,\"prepare_model_access_complete\":true"
      <<",\"all_physical_tables_allocated\":true,\"physical_entries\":"<<layout.count
      <<",\"physical_table_bytes\":"<<layout.count*25
      <<",\"epoch_seconds\":"<<epoch_seconds()<<",\"diagnostic_only\":true}\n";
    atomic_json(ready,readyJson.str());
    std::cout<<"benchmark_ready,pid,"<<getpid()<<",path,"<<ready<<std::endl;
    barrierWait=wait_for_file(barrier/"GO",barrierTimeout,"start barrier");
    goObservedEpoch=epoch_seconds();
  }
  waveTarget=cfg.steps;
  rusage before{},after{};
  if(getrusage(RUSAGE_SELF,&before))throw std::runtime_error("getrusage failed");
  started=Clock::now();
  const double computeStartEpoch=epoch_seconds(),computeStartMonotonic=monotonic_seconds();
  std::cout<<"benchmark_compute_start,pid,"<<getpid()<<",epoch_seconds,"<<std::setprecision(17)<<computeStartEpoch
           <<",monotonic_seconds,"<<computeStartMonotonic<<std::endl;
  std::vector<std::thread> threads;
  std::vector<exploration_candidate_cache::Counters> cacheCounters(cfg.threads);
  for(int i=0;i<cfg.threads;++i)threads.emplace_back([&,i] {
    run_worker(i);cacheCounters[i]=exploration_candidate_cache::counters();
  });
  for(auto& thread:threads)thread.join();
  const double computeSeconds=std::chrono::duration<double>(Clock::now()-started).count();
  const double computeEndEpoch=epoch_seconds(),computeEndMonotonic=monotonic_seconds();
  if(getrusage(RUSAGE_SELF,&after))throw std::runtime_error("getrusage failed");
  if(workerFailed.load())throw std::runtime_error("benchmark worker failed");
  for(int i=0;i<cfg.threads;++i)flush_window(i,true);
  checked_close(summaryFile,"benchmark summary");checked_close(episodeFile,"benchmark episodes");checked_close(sampleFile,"benchmark samples");
  checked_close(allocationFile,"benchmark allocation");
  const double cpu=cpu_seconds(after)-cpu_seconds(before);
  exploration_candidate_cache::Counters cacheTotal;
  for(const auto& count:cacheCounters) {
    cacheTotal.stored_turns+=count.stored_turns;cacheTotal.fallback_turns+=count.fallback_turns;
    cacheTotal.stored_legal_candidates+=count.stored_legal_candidates;
    cacheTotal.allocated_bytes+=count.allocated_bytes;
    cacheTotal.capacity_turns=std::max(cacheTotal.capacity_turns,count.capacity_turns);
  }
  if(const char* fp=std::getenv("PROBE_FINGERPRINT");fp && std::strcmp(fp,"1")==0) {extern void probe_fingerprint();probe_fingerprint();}
  double nativeSaveSeconds=0,nativeShaSeconds=0,nativeStartEpoch=0,nativeEndEpoch=0;
  unsigned long long nativeBytes=0;
  unsigned long long nativeRssBefore=0,nativeRssAfter=0;
  long nativePeakRss=0;
  std::string nativeHash;
  if(!nativeProbe.empty()) {
    fs::create_directories(nativeProbe.parent_path());
    nativeStartEpoch=epoch_seconds();nativeRssBefore=current_rss_bytes();const auto saveStart=Clock::now();
#ifdef NT9_COMPACT_STORAGE
    if(!save_exploration_compact_state(nativeProbe.c_str()))throw std::runtime_error("diagnostic native model save failed");
#endif
    nativeSaveSeconds=std::chrono::duration<double>(Clock::now()-saveStart).count();
    const auto shaStart=Clock::now();nativeHash=sha256_file(nativeProbe);
    nativeShaSeconds=std::chrono::duration<double>(Clock::now()-shaStart).count();
    nativeBytes=fs::file_size(nativeProbe);nativeEndEpoch=epoch_seconds();
    nativeRssAfter=current_rss_bytes();rusage probeUsage{};
    if(getrusage(RUSAGE_SELF,&probeUsage))throw std::runtime_error("probe getrusage failed");
    nativePeakRss=probeUsage.ru_maxrss;
  }
  const auto writeResult=[&](const char* holdStatus,bool success,double holdSeconds) {
  std::ostringstream result;
  result<<std::setprecision(17)<<"{\"status\":"<<json_string(success?"passed":"failed")
        <<",\"diagnostic_only\":true,\"checkpoints_written\":false,\"research_checkpoints_written\":false,"
        <<"\"native_model_only\":"<<(!nativeProbe.empty()?"true":"false")<<','
        <<"\"fresh_initialization\":true,\"requested_steps\":"<<cfg.steps
        <<",\"pid\":"<<getpid()<<",\"starttick\":"<<process_starttick()
        <<",\"actual_steps\":"<<completed.load()<<",\"workers\":"<<cfg.threads
        <<",\"model_access\":"<<json_string(cfg.model_access)
        <<",\"feature_cache_mib_per_worker\":"<<cfg.feature_cache_mib
        <<",\"feature_cache_specification\":"<<json_string(exploration_candidate_cache::specification)
        <<",\"feature_cache_stored_turns\":"<<cacheTotal.stored_turns
        <<",\"feature_cache_fallback_turns\":"<<cacheTotal.fallback_turns
        <<",\"feature_cache_stored_legal_candidates\":"<<cacheTotal.stored_legal_candidates
        <<",\"feature_cache_allocated_bytes_total\":"<<cacheTotal.allocated_bytes
        <<",\"feature_cache_max_capacity_turns_per_worker\":"<<cacheTotal.capacity_turns
        <<",\"behavior\":"<<json_string(cfg.behavior)<<",\"formula\":"<<json_string(cfg.formula)
        <<",\"sample_every\":"<<cfg.sample_every<<",\"summary_interval\":"<<cfg.summary
        <<",\"initialize_seconds\":"<<initializeSeconds<<",\"compute_seconds\":"<<computeSeconds
        <<",\"compute_start_epoch_seconds\":"<<computeStartEpoch<<",\"compute_end_epoch_seconds\":"<<computeEndEpoch
        <<",\"compute_start_monotonic_seconds\":"<<computeStartMonotonic<<",\"compute_end_monotonic_seconds\":"<<computeEndMonotonic
        <<",\"start_barrier\":"<<json_string(barrier.string())<<",\"barrier_wait_seconds\":"<<barrierWait
        <<",\"barrier_go_observed_epoch_seconds\":"<<goObservedEpoch
        <<",\"steps_per_second\":"<<completed.load()/computeSeconds
        <<",\"compute_cpu_seconds\":"<<cpu<<",\"compute_cpu_wall_ratio\":"<<cpu/computeSeconds
        <<",\"peak_rss_kib\":"<<after.ru_maxrss
        <<",\"compute_minor_faults\":"<<after.ru_minflt-before.ru_minflt
        <<",\"compute_major_faults\":"<<after.ru_majflt-before.ru_majflt
        <<",\"compute_voluntary_context_switches\":"<<after.ru_nvcsw-before.ru_nvcsw
        <<",\"compute_involuntary_context_switches\":"<<after.ru_nivcsw-before.ru_nivcsw
        <<",\"source_sha256\":"<<json_string(cfg.source)<<",\"binary_sha256\":"<<json_string(binaryHash)
        <<",\"identity\":"<<json_string(cfg.identity())
        <<",\"hold_until\":"<<json_string(holdUntil.string())<<",\"hold_status\":"<<json_string(holdStatus)
        <<",\"hold_timeout_seconds\":600,\"hold_wait_seconds\":"<<holdSeconds
        <<",\"native_probe_file\":"<<json_string(nativeProbe.string())<<",\"native_probe_bytes\":"<<nativeBytes
        <<",\"native_probe_sha256\":"<<json_string(nativeHash)<<",\"native_probe_save_seconds\":"<<nativeSaveSeconds
        <<",\"native_probe_sha_seconds\":"<<nativeShaSeconds<<",\"native_probe_total_seconds\":"<<nativeSaveSeconds+nativeShaSeconds
        <<",\"native_probe_start_epoch_seconds\":"<<nativeStartEpoch<<",\"native_probe_end_epoch_seconds\":"<<nativeEndEpoch
        <<",\"native_probe_rss_before_bytes\":"<<nativeRssBefore<<",\"native_probe_rss_after_bytes\":"<<nativeRssAfter
        <<",\"native_probe_peak_rss_kib\":"<<nativePeakRss
        <<",\"limitations\":\"fresh model; restart groups can overshoot; initialization/barrier/CSV close/save/hold excluded from compute; optional native save is model-only and tests peer RAM retention, not concurrent peer learning during save\"}\n";
  atomic_json(cfg.output/"benchmark.json",result.str());
  };
  writeResult(holdUntil.empty()?"not_requested":"waiting",true,0);
  if(!holdUntil.empty()) {
    try {const double held=wait_for_file(holdUntil,600,"RAM hold");writeResult("released",true,held);}
    catch(...){writeResult("timeout",false,600);throw;}
  }
  std::cout<<"benchmark_complete,workers,"<<cfg.threads<<",access,"<<cfg.model_access
           <<",steps,"<<completed.load()<<",compute_seconds,"<<computeSeconds
           <<",steps_per_sec,"<<completed.load()/computeSeconds<<",cpu_wall_ratio,"<<cpu/computeSeconds<<'\n';
  return 0;
 } catch(const std::exception& e){std::cerr<<"exploration benchmark failed: "<<e.what()<<'\n';return 1;}
}
