#ifndef SPAWN_BATCH_ARCHIVE_H
#define SPAWN_BATCH_ARCHIVE_H
#include "staged_archive_curriculum.h"
#include <mutex>
#include <functional>
#include <random>
#include <iomanip>
#include <iostream>
namespace spawn_batch_archive {
// One source afterstate expands to 2*empty cells, shuffled without replacement.
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
  std::string digest;
  std::function<std::string(const Archive&,long long)> publish;
  void configure(int seed,int workers){std::seed_seq seq{static_cast<unsigned>(seed),0x41524348u};rng.seed(seq);reservations.assign(workers,-1);}
  bool all_done()const{return !state.empty()&&std::all_of(state.begin(),state.end(),[](int s){return s==2;});}
  void refresh(long long step){
    auto next=curriculum.take_target_source();
    if(next){source=std::move(next);active_target=curriculum.desired_exponent;}
    else if(source){++starvation;}
    else if(!bootstrap.empty()){
      source=staged_archive_curriculum::Source{bootstrap[std::uniform_int_distribution<size_t>(0,bootstrap.size()-1)(rng)],0};active_target=14;
      if(curriculum.desired_exponent!=14)++starvation;
    }else return;
    staged_archive_curriculum::validate(source->entry,active_target);
    active.clear();
    for(int cell=0;cell<16;++cell)if(source->entry.board[cell]==0)for(int tile:{1,2}){
      Entry e=source->entry;e.board[cell]=tile;e.board_id+="_spawn_c"+std::to_string(cell)+"_e"+std::to_string(tile);active.push_back(std::move(e));
    }
    std::shuffle(active.begin(),active.end(),rng);state.assign(active.size(),0);++generation;
    if(publish)digest=publish(*this,step);
  }
  template<class Engine> bool draw(Engine&,Entry& entry,unsigned long long& epoch,std::string& sha,int worker,long long step){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    if(worker<0||static_cast<size_t>(worker)>=reservations.size()||reservations[worker]!=-1)throw std::runtime_error("invalid archive worker reservation");
    if(active.empty()||all_done())refresh(step);
    const auto available=std::find(state.begin(),state.end(),0);
    if(available==state.end()){++fallbacks;if(!active.empty())++busy_fallbacks;return false;}
    const int index=static_cast<int>(available-state.begin());state[index]=1;reservations[worker]=index;
    entry=active[index];epoch=generation;sha=digest;return true;
  }
  void complete(int worker,long long step){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    if(worker<0||static_cast<size_t>(worker)>=reservations.size())throw std::runtime_error("invalid archive completion worker");
    const int i=reservations[worker];if(i<0)return;
    if(static_cast<size_t>(i)>=state.size()||state[i]!=1)throw std::runtime_error("invalid archive completion state");
    state[i]=2;reservations[worker]=-1;
  }
  void record_original(bool normal,int maximum,long long step){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    if(curriculum.record_original(normal,maximum)&&publish){
      // Audit stage decision separately: active batch must finish before any
      // change to its content/hash. A generation SHA never changes mid-batch.
      std::cout<<"archive_stage_promoted,step,"<<step<<",desired_exponent,"<<curriculum.desired_exponent<<",normal_window,"<<curriculum.window.size()<<",count32,"<<curriculum.count32<<",count64,"<<curriculum.count64<<std::endl;
    }
  }
  void capture(Entry entry,long long step,bool normal,int restart,unsigned& seen){
    std::lock_guard<std::mutex> lock(mutex);last_step=step;
    const auto before=curriculum.rejected;
    std::array<std::string,3> ids;for(int i=0;i<3;++i)if(curriculum.next_sources[i])ids[i]=curriculum.next_sources[i]->entry.board_id;
    curriculum.capture_crossings(normal,restart,seen,std::move(entry),step);rejected+=curriculum.rejected-before;
    for(int i=0;i<3;++i)if(curriculum.next_sources[i]&&curriculum.next_sources[i]->entry.board_id!=ids[i]){++captured;if(first_capture_step<0)first_capture_step=step;}
  }
  static void write_entry(std::ostream& out,const Entry& e){out<<std::quoted(e.board_id)<<' '<<std::quoted(e.source_game)<<' '<<e.source_turn<<' '<<e.cumulative_score;for(int x:e.board)out<<' '<<x;out<<'\n';}
  static Entry read_entry(std::istream& in){Entry e;in>>std::quoted(e.board_id)>>std::quoted(e.source_game)>>e.source_turn>>e.cumulative_score;for(int& x:e.board)in>>x;if(!in)throw std::runtime_error("truncated archive batch entry");return e;}
  void save(std::ostream& out){
    std::lock_guard<std::mutex> lock(mutex);
    if(std::any_of(reservations.begin(),reservations.end(),[](int i){return i!=-1;}))throw std::runtime_error("checkpoint with in-flight archive reservations");
    out<<"SPAWN_BATCH_V1 "<<generation<<' '<<active_target<<' '<<captured<<' '<<rejected<<' '<<fallbacks<<' '<<starvation<<' '<<busy_fallbacks<<' '<<first_capture_step<<' '<<last_step<<' '<<std::quoted(digest)<<'\n'<<rng<<'\n';
    curriculum.save(out);out<<bool(source)<<'\n';if(source){out<<source->captured_step<<'\n';write_entry(out,source->entry);}
    out<<active.size()<<'\n';for(size_t i=0;i<active.size();++i){out<<state[i]<<' ';write_entry(out,active[i]);}
    out<<reservations.size()<<'\n';for(int i:reservations)out<<i<<' ';out<<'\n';
  }
  void load(std::istream& in){
    std::lock_guard<std::mutex> lock(mutex);std::string magic;in>>magic>>generation>>active_target>>captured>>rejected>>fallbacks>>starvation>>busy_fallbacks>>first_capture_step>>last_step>>std::quoted(digest)>>rng;
    if(!in||magic!="SPAWN_BATCH_V1"||active_target<14||active_target>16)throw std::runtime_error("invalid archive batch header");
    curriculum.load(in);int has;in>>has;if(!in||(has!=0&&has!=1))throw std::runtime_error("invalid archive source marker");source.reset();
    if(has){staged_archive_curriculum::Source s;in>>s.captured_step;s.entry=read_entry(in);staged_archive_curriculum::validate(s.entry,active_target);source=std::move(s);}
    size_t n;in>>n;if(!in||n>30||(!source&&n))throw std::runtime_error("invalid archive batch size");active.clear();state.clear();std::set<std::array<int,16>> boards;
    for(size_t i=0;i<n;++i){int status;in>>status;auto e=read_entry(in);if(status!=0&&status!=2)throw std::runtime_error("saved archive batch has in-flight/invalid status");if(!boards.insert(e.board).second)throw std::runtime_error("duplicate archive variant");
      int changed=0;for(int c=0;c<16;++c)if(e.board[c]!=source->entry.board[c]){if(source->entry.board[c]||!(e.board[c]==1||e.board[c]==2))throw std::runtime_error("invalid spawned variant");++changed;}
      if(changed!=1||e.source_turn!=source->entry.source_turn||e.source_game!=source->entry.source_game||e.cumulative_score!=source->entry.cumulative_score)throw std::runtime_error("invalid archive variant provenance");active.push_back(std::move(e));state.push_back(status);
    }
    if(source&&n!=2*std::count(source->entry.board.begin(),source->entry.board.end(),0))throw std::runtime_error("incomplete archive batch variants");
    size_t workers;in>>workers;if(!in||workers!=reservations.size())throw std::runtime_error("archive worker count mismatch");for(int& i:reservations){in>>i;if(!in||i!=-1)throw std::runtime_error("invalid saved archive reservation");}
  }
};
}
#endif
