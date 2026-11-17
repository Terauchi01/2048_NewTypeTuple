#ifndef STAGED_ARCHIVE_CURRICULUM_H
#define STAGED_ARCHIVE_CURRICULUM_H
#include "start_board_pool.h"
#include <deque>
#include <optional>
#include <iomanip>

// Pure checkpointable curriculum state. Caller holds the archive mutex.
// Batch generation/claim/commit is intentionally separate: 28 transforms have
// not yet been specified, so this module never invents transformed boards.
namespace staged_archive_curriculum {
inline void validate(const start_pool::Entry& e,int target) {
  if(target<14||target>16||e.board_id.empty()||e.source_game.empty()||e.source_turn<=0||e.cumulative_score<0)
    throw std::invalid_argument("invalid staged source provenance/target");
  int maximum=0,count=0,empty=0;
  for(int x:e.board){if(x<0||x>17)throw std::invalid_argument("invalid staged source exponent");maximum=std::max(maximum,x);count+=x==target;empty+=x==0;}
  if(maximum!=target||count!=1||!empty)throw std::invalid_argument("staged source must be a first target afterstate with an empty cell");
  for(int cell=0;cell<16;++cell)if(!e.board[cell])for(int tile:{1,2}){
    board_t b;start_pool::copy_board(e,b);b[cell]=tile;bool legal=false;
    for(int direction=0;direction<4;++direction){board_t next;if(moveB(b,next,static_cast<move_dir>(direction))>=0)legal=true;}
    if(!legal)throw std::invalid_argument("staged source may terminate before first action");
  }
}
inline bool needs_capture(int maximum,unsigned seen){for(int target=14;target<=16;++target)if(maximum>=target&&!(seen&(1u<<(target-14))))return true;return false;}
struct Source {start_pool::Entry entry;long long captured_step=0;};
struct Curriculum {
  static constexpr size_t window_size=1000;
  unsigned promote32_count=200,promote64_count=501;
  void configure_percent(unsigned minimum32,unsigned strictly_above64){
    if(minimum32>strictly_above64||strictly_above64>=100)throw std::invalid_argument("archive promotion percentages require0<=32<=64<100");
    promote32_count=(window_size*minimum32+99)/100;promote64_count=window_size*strictly_above64/100+1;
  }
  int desired_exponent=14;
  std::deque<unsigned char> window;
  unsigned count32=0,count64=0;
  unsigned long long normal_games=0,rejected=0,replaced=0;
  std::array<std::optional<Source>,3> next_sources;
  bool record_original(bool normal,int max_exponent) {
    if(!normal)return false;
    if(max_exponent<0||max_exponent>17)throw std::invalid_argument("invalid original maximum");
    ++normal_games;window.push_back(static_cast<unsigned char>(max_exponent));count32+=max_exponent>=15;count64+=max_exponent>=16;
    if(window.size()>window_size){const int old=window.front();window.pop_front();count32-=old>=15;count64-=old>=16;}
    const int before=desired_exponent;
    if(window.size()==window_size){if(desired_exponent==14&&count32>=promote32_count)desired_exponent=15;if(desired_exponent==15&&count32>=promote64_count)desired_exponent=16;}
    return desired_exponent!=before;
  }
  // Mark the first crossing before checking validity; never replace an unsafe
  // first crossing with a later convenient position from that same game.
  void capture_crossings(bool normal,int restart,unsigned& seen,start_pool::Entry entry,long long step) {
    if(!normal||restart!=0)return;
    const int maximum=*std::max_element(entry.board.begin(),entry.board.end());
    for(int target=14;target<=16;++target){const unsigned bit=1u<<(target-14);if(maximum<target||(seen&bit))continue;seen|=bit;
      try{validate(entry,target);}catch(const std::exception&){++rejected;continue;}
      auto& slot=next_sources[target-14];if(slot)++replaced;slot=Source{entry,step};
    }
  }
  bool target_source_available() const {return next_sources[desired_exponent-14].has_value();}
  std::optional<Source> take_target_source(){auto& slot=next_sources[desired_exponent-14];auto result=std::move(slot);slot.reset();return result;}
  static void write_source(std::ostream& out,const Source& s){const auto& e=s.entry;out<<s.captured_step<<' '<<std::quoted(e.board_id)<<' '<<std::quoted(e.source_game)<<' '<<e.source_turn<<' '<<e.cumulative_score;for(int x:e.board)out<<' '<<x;out<<'\n';}
  void save(std::ostream& out) const {
    out<<"STAGED_CURRICULUM_V1 "<<desired_exponent<<' '<<normal_games<<' '<<rejected<<' '<<replaced<<' '<<window.size()<<'\n';
    for(auto x:window){out<<static_cast<int>(x)<<' ';}out<<'\n';
    for(const auto& source:next_sources){out<<bool(source)<<'\n';if(source)write_source(out,*source);}
  }
  void load(std::istream& in){
    std::string magic;size_t n;in>>magic>>desired_exponent>>normal_games>>rejected>>replaced>>n;
    if(!in||magic!="STAGED_CURRICULUM_V1"||desired_exponent<14||desired_exponent>16||n>window_size||n!=std::min<unsigned long long>(normal_games,window_size))throw std::runtime_error("invalid staged curriculum checkpoint");
    window.clear();count32=count64=0;
    for(size_t i=0;i<n;++i){int x;in>>x;if(!in||x<0||x>17)throw std::runtime_error("invalid staged curriculum window");window.push_back(static_cast<unsigned char>(x));count32+=x>=15;count64+=x>=16;}
    for(int index=0;index<3;++index){int present;in>>present;if(!in||(present!=0&&present!=1))throw std::runtime_error("invalid staged source marker");next_sources[index].reset();if(present){Source s;auto& e=s.entry;in>>s.captured_step>>std::quoted(e.board_id)>>std::quoted(e.source_game)>>e.source_turn>>e.cumulative_score;for(int& x:e.board)in>>x;if(!in||s.captured_step<0)throw std::runtime_error("invalid staged source state");validate(e,index+14);next_sources[index]=std::move(s);}}
  }
};
}
#endif
