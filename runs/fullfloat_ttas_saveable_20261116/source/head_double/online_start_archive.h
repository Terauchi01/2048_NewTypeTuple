#ifndef ONLINE_START_ARCHIVE_H
#define ONLINE_START_ARCHIVE_H
#include "start_board_pool.h"
#include <mutex>
#include <functional>
#include <iomanip>

// Latest-unique FIFO, not a lifetime-uniform reservoir. All access is guarded;
// draws return values so publishing a new generation cannot invalidate a game.
namespace online_start_archive {
struct Archive {
  using Entry=start_pool::Entry;
  std::mutex mutex;
  size_t capacity=1024;
  long long interval=1000000000LL,next_update=1000000000LL;
  unsigned long long generation=0,captured=0,rejected=0,duplicates=0,evicted=0,fallbacks=0;
  long long first_capture_step=-1;
  std::vector<Entry> active,pending;
  std::string digest;
  std::function<std::string(const Archive&,long long)> publish;
  static bool same(const Entry& a,const Entry& b){return a.board==b.board;}
  bool contains(const Entry& e) const {
    return std::any_of(active.begin(),active.end(),[&](const Entry& x){return same(e,x);})||
      std::any_of(pending.begin(),pending.end(),[&](const Entry& x){return same(e,x);});
  }
  void capture(Entry e,long long step) {
    // Validate exactly the first crossing; callers mark it seen before calling.
    try{start_pool::validate(e);}catch(const std::exception&){std::lock_guard<std::mutex> lock(mutex);++rejected;return;}
    std::lock_guard<std::mutex> lock(mutex);
    if(contains(e)){++duplicates;return;}
    if(first_capture_step<0)first_capture_step=step;
    ++captured;pending.push_back(std::move(e));
    if(pending.size()>capacity){pending.erase(pending.begin());++evicted;}
  }
  template<class Engine> bool draw(Engine& rng,Entry& entry,unsigned long long& epoch,std::string& sha) {
    std::lock_guard<std::mutex> lock(mutex);
    if(active.empty()){++fallbacks;return false;}
    entry=active[std::uniform_int_distribution<size_t>(0,active.size()-1)(rng)];epoch=generation;sha=digest;return true;
  }
  void advance(long long step) {
    std::lock_guard<std::mutex> lock(mutex);
    if(step<next_update)return;
    for(auto& e:pending)active.push_back(std::move(e));
    pending.clear();
    if(active.size()>capacity){evicted+=active.size()-capacity;active.erase(active.begin(),active.end()-capacity);}
    // Skip vacant boundaries deterministically; publish one current generation.
    generation=static_cast<unsigned long long>(step/interval);
    next_update=(step/interval+1)*interval;
    if(publish)digest=publish(*this,step);
  }
  void write_entries(std::ostream& out,const std::vector<Entry>& es) const {
    out<<es.size()<<'\n';
    for(const auto& e:es){out<<std::quoted(e.board_id)<<' '<<std::quoted(e.source_game)<<' '<<e.source_turn<<' '<<e.cumulative_score;for(int x:e.board)out<<' '<<x;out<<'\n';}
  }
  void save(std::ostream& out) {
    std::lock_guard<std::mutex> lock(mutex);
    out<<"ONLINE_FIFO_V1 "<<capacity<<' '<<interval<<' '<<next_update<<' '<<generation<<' '<<captured<<' '<<rejected<<' '<<duplicates<<' '<<evicted<<' '<<fallbacks<<' '<<first_capture_step<<' '<<std::quoted(digest)<<'\n';
    write_entries(out,active);write_entries(out,pending);
  }
  void load(std::istream& in) {
    std::lock_guard<std::mutex> lock(mutex);
    std::string magic;size_t cap;long long period;
    in>>magic>>cap>>period>>next_update>>generation>>captured>>rejected>>duplicates>>evicted>>fallbacks>>first_capture_step>>std::quoted(digest);
    if(magic!="ONLINE_FIFO_V1"||cap!=capacity||period!=interval||next_update<=0||next_update%interval||generation!=static_cast<unsigned long long>(next_update/interval-1))throw std::runtime_error("invalid online archive schedule");
    auto entries=[&](std::vector<Entry>& es){size_t n;in>>n;if(!in||n>capacity)throw std::runtime_error("invalid online archive capacity");es.clear();for(size_t i=0;i<n;++i){Entry e;in>>std::quoted(e.board_id)>>std::quoted(e.source_game)>>e.source_turn>>e.cumulative_score;for(int& x:e.board)in>>x;if(!in)throw std::runtime_error("truncated online archive state");start_pool::validate(e);es.push_back(std::move(e));}};
    entries(active);entries(pending);
    for(size_t i=0;i<active.size();++i)for(size_t j=0;j<i;++j)if(same(active[i],active[j]))throw std::runtime_error("duplicate online active board");
    for(size_t i=0;i<pending.size();++i){for(const auto& a:active)if(same(pending[i],a))throw std::runtime_error("duplicate online pending board");for(size_t j=0;j<i;++j)if(same(pending[i],pending[j]))throw std::runtime_error("duplicate online pending board");}
  }
};
}
#endif
