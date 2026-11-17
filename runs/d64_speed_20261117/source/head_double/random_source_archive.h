#ifndef RANDOM_SOURCE_ARCHIVE_H
#define RANDOM_SOURCE_ARCHIVE_H
#include "staged_archive_curriculum.h"
#include <mutex>
#include <functional>
#include <random>
#include <iomanip>
#include <iostream>
namespace random_source_archive {
// One afterstate supplies 128 independent actual-game spawns (with replacement).
// Each claim lasts for one entire original+restart training episode.
struct Archive {
  using Entry=start_pool::Entry;
  std::mutex mutex;
  staged_archive_curriculum::Curriculum curriculum;
  std::vector<Entry> bootstrap,active;
  std::optional<staged_archive_curriculum::Source> source;
  std::vector<int> state,reservations;
  std::mt19937 rng;
  unsigned long long generation=0,captured=0,rejected=0,duplicates=0,evicted=0,fallbacks=0,starvation=0,busy_fallbacks=0;
  long long first_capture_step=-1,last_step=0;
  int active_target=14;
  std::string digest,source_board_sha;
  std::function<std::string(Archive&,long long)> publish;
  std::function<void(const Archive&,long long,const std::string&)> availability_changed;
  std::string last_availability;
  int last_availability_target=-1;
  // Caller holds mutex; this observes whether draw can obtain a source now.
  std::string availability_state() const {
    if(std::find(state.begin(),state.end(),0)!=state.end())return "available";
    if(!active.empty()&&!all_done())return "batch_busy";
    if(curriculum.target_source_available() ||
       (curriculum.desired_exponent==16&&curriculum.next_sources[1]) ||
       (!source&&!bootstrap.empty()&&curriculum.desired_exponent==14))return "available";
    return "source_missing";
  }
  void notify_availability(long long step) {
    if(!availability_changed)return;
    const auto status=availability_state();
    if(status!=last_availability||curriculum.desired_exponent!=last_availability_target) {
      availability_changed(*this,step,status);last_availability=status;last_availability_target=curriculum.desired_exponent;
    }
  }
  void configure(int seed,int workers){std::seed_seq seq{static_cast<unsigned>(seed),0x41524348u};rng.seed(seq);reservations.assign(workers,-1);}
  bool all_done()const{return !state.empty()&&std::all_of(state.begin(),state.end(),[](int s){return s==2;});}
  void refresh(long long step){
    auto next=curriculum.take_target_source();int next_target=curriculum.desired_exponent;
    if(!next&&curriculum.desired_exponent==16&&curriculum.next_sources[1]){next=std::move(curriculum.next_sources[1]);curriculum.next_sources[1].reset();next_target=15;}
    if(next){source=std::move(next);active_target=next_target;}
    else if(!source&&!bootstrap.empty()&&curriculum.desired_exponent==14){
      source=staged_archive_curriculum::Source{bootstrap[std::uniform_int_distribution<size_t>(0,bootstrap.size()-1)(rng)],0};active_target=14;
    }else{++starvation;return;}
    staged_archive_curriculum::validate(source->entry,active_target);
    active.clear();
    for(int trial=0;trial<128;++trial){
      Entry e=source->entry;e.board_id+="_trial_"+std::to_string(trial);active.push_back(std::move(e));
    }
    state.assign(active.size(),0);++generation;
    if(publish)digest=publish(*this,step);
  }
  template<class Engine> bool draw(Engine&,Entry& entry,unsigned long long& epoch,std::string& sha,int worker,long long step,std::string* fallback_reason=nullptr){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    if(fallback_reason)*fallback_reason="none";
    if(worker<0||static_cast<size_t>(worker)>=reservations.size()||reservations[worker]!=-1)throw std::runtime_error("invalid archive worker reservation");
    if(active.empty()||all_done())refresh(step);
    const auto available=std::find(state.begin(),state.end(),0);
    if(available==state.end()){++fallbacks;if(!active.empty()&&!all_done())++busy_fallbacks;
      if(fallback_reason)*fallback_reason=(!active.empty()&&!all_done())?"batch_busy":"source_missing";
      if((fallbacks&(fallbacks-1))==0)std::cout<<"archive_normal_fallback,step,"<<step<<",generation,"<<generation<<",fallbacks,"<<fallbacks<<",next_source_missing,"<<starvation<<",batch_busy,"<<busy_fallbacks<<std::endl;
      notify_availability(step);return false;}
    const int index=static_cast<int>(available-state.begin());state[index]=1;reservations[worker]=index;
    entry=active[index];epoch=generation;sha=digest;notify_availability(step);return true;
  }
  void complete(int worker,long long step){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    if(worker<0||static_cast<size_t>(worker)>=reservations.size())throw std::runtime_error("invalid archive completion worker");
    const int i=reservations[worker];if(i<0)return;
    if(static_cast<size_t>(i)>=state.size()||state[i]!=1)throw std::runtime_error("invalid archive completion state");
    state[i]=2;reservations[worker]=-1;
    notify_availability(step);
    if(all_done())std::cout<<"archive_source_finished,step,"<<step<<",generation,"<<generation<<",completed_original_episodes,128,source_board_sha256,"<<source_board_sha<<",target,"<<active_target<<",source_turn,"<<source->entry.source_turn<<std::endl;
  }
  void record_original(bool normal,int maximum,long long step){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    if(curriculum.record_original(normal,maximum)&&publish){
      // Audit stage decision separately: active batch must finish before any
      // change to its content/hash. A generation SHA never changes mid-batch.
      std::cout<<"archive_stage_promoted,step,"<<step<<",desired_exponent,"<<curriculum.desired_exponent<<",normal_window,"<<curriculum.window.size()<<",count32,"<<curriculum.count32<<",count64,"<<curriculum.count64<<std::endl;
    }
    notify_availability(step);
  }
  void capture(Entry entry,long long step,bool normal,int restart,unsigned& seen){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    const int maximum=*std::max_element(entry.board.begin(),entry.board.end());
    bool new_crossing=false;for(int target=14;target<=16;++target)if(maximum>=target&&!(seen&(1u<<(target-14))))new_crossing=true;
    if(normal&&restart==0&&new_crossing){
      bool duplicate=source&&entry.board==source->entry.board;
      for(const auto& next:curriculum.next_sources)if(next&&next->entry.board==entry.board)duplicate=true;
      if(duplicate){for(int target=14;target<=16;++target)if(maximum>=target)seen|=1u<<(target-14);++duplicates;
        if((duplicates&(duplicates-1))==0)std::cout<<"archive_source_duplicate_rejected,step,"<<step<<",count,"<<duplicates<<std::endl;
        return;
      }
    }
    const auto before=curriculum.rejected;
    std::array<std::string,3> ids;for(int i=0;i<3;++i)if(curriculum.next_sources[i])ids[i]=curriculum.next_sources[i]->entry.board_id;
    curriculum.capture_crossings(normal,restart,seen,std::move(entry),step);rejected+=curriculum.rejected-before;
    for(int i=0;i<3;++i)if(curriculum.next_sources[i]&&curriculum.next_sources[i]->entry.board_id!=ids[i]){++captured;if(first_capture_step<0)first_capture_step=step;}
    notify_availability(step);
  }
  static void write_entry(std::ostream& out,const Entry& e){out<<std::quoted(e.board_id)<<' '<<std::quoted(e.source_game)<<' '<<e.source_turn<<' '<<e.cumulative_score;for(int x:e.board)out<<' '<<x;out<<'\n';}
  static Entry read_entry(std::istream& in){Entry e;in>>std::quoted(e.board_id)>>std::quoted(e.source_game)>>e.source_turn>>e.cumulative_score;for(int& x:e.board)in>>x;if(!in)throw std::runtime_error("truncated archive batch entry");return e;}
  void save(std::ostream& out){
    std::lock_guard<std::mutex> lock(mutex);
    if(std::any_of(reservations.begin(),reservations.end(),[](int i){return i!=-1;}))throw std::runtime_error("checkpoint with in-flight archive reservations");
    out<<"RANDOM_SOURCE128_V1 "<<generation<<' '<<active_target<<' '<<captured<<' '<<rejected<<' '<<duplicates<<' '<<fallbacks<<' '<<starvation<<' '<<busy_fallbacks<<' '<<first_capture_step<<' '<<last_step<<' '<<std::quoted(digest)<<' '<<std::quoted(source_board_sha)<<'\n'<<rng<<'\n';
    curriculum.save(out);out<<bool(source)<<'\n';if(source){out<<source->captured_step<<'\n';write_entry(out,source->entry);}
    out<<active.size()<<'\n';for(size_t i=0;i<active.size();++i){out<<state[i]<<' ';write_entry(out,active[i]);}
    out<<reservations.size()<<'\n';for(int i:reservations)out<<i<<' ';out<<'\n';
  }
  void load(std::istream& in){
    std::lock_guard<std::mutex> lock(mutex);std::string magic;in>>magic>>generation>>active_target>>captured>>rejected>>duplicates>>fallbacks>>starvation>>busy_fallbacks>>first_capture_step>>last_step>>std::quoted(digest)>>std::quoted(source_board_sha)>>rng;
    if(!in||magic!="RANDOM_SOURCE128_V1"||active_target<14||active_target>16)throw std::runtime_error("invalid archive batch header");
    curriculum.load(in);int has;in>>has;if(!in||(has!=0&&has!=1))throw std::runtime_error("invalid archive source marker");source.reset();
    if(has){staged_archive_curriculum::Source s;in>>s.captured_step;s.entry=read_entry(in);staged_archive_curriculum::validate(s.entry,active_target);source=std::move(s);}
    size_t n;in>>n;if(!in||n>128||(!source&&n))throw std::runtime_error("invalid archive batch size");active.clear();state.clear();std::set<std::string> ids;
    for(size_t i=0;i<n;++i){int status;in>>status;auto e=read_entry(in);if(status!=0&&status!=2)throw std::runtime_error("saved archive batch has in-flight/invalid status");if(!ids.insert(e.board_id).second)throw std::runtime_error("duplicate archive trial ID");
      if(e.board!=source->entry.board||e.source_turn!=source->entry.source_turn||e.source_game!=source->entry.source_game||e.cumulative_score!=source->entry.cumulative_score)throw std::runtime_error("invalid archive trial afterstate/provenance");active.push_back(std::move(e));state.push_back(status);
    }
    if(source&&n!=128)throw std::runtime_error("incomplete archive batch variants");
    size_t workers;in>>workers;if(!in||workers!=reservations.size())throw std::runtime_error("archive worker count mismatch");for(int& i:reservations){in>>i;if(!in||i!=-1)throw std::runtime_error("invalid saved archive reservation");}
  }
};
}
#endif
