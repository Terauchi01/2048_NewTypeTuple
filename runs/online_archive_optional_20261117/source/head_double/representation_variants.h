#ifndef REPRESENTATION_VARIANTS_H
#define REPRESENTATION_VARIANTS_H
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace representation {
inline std::string mode="control";
inline bool evaluation_mask=false;
inline long long switch_step=25000000000LL;
inline int stage_threshold=13;
inline std::string pool_path, pool_sha="-";
using Board=std::array<int,16>;
inline std::vector<std::vector<Board>> bands;
inline bool native_evaluator_required(){return mode=="stage-boundary"||mode=="high-tiles"||mode=="aux-2x2";}
inline bool valid_mode(const std::string& m){return m=="control"||m=="progress-mix"||m=="restart-schedule"||m=="stage-boundary"||m=="high-tiles"||m=="aux-2x2";}
inline void validate(){if(!valid_mode(mode)||switch_step<0||stage_threshold<1||stage_threshold>17)throw std::invalid_argument("invalid representation configuration");if(mode!="stage-boundary"&&stage_threshold!=13)throw std::invalid_argument("stage threshold belongs to stage-boundary mode");if(mode!="restart-schedule"&&switch_step!=25000000000LL)throw std::invalid_argument("restart switch belongs to restart-schedule mode");if(mode!="progress-mix"&&!pool_path.empty())throw std::invalid_argument("pool only supported by progress-mix");}
inline std::string signature(){return mode+":"+std::to_string(switch_step)+":"+std::to_string(stage_threshold)+":"+pool_sha;}
inline int stage(int exponent){return exponent>=(mode=="stage-boundary"?stage_threshold:14);}
// Same table capacity: last VSE channel reallocates low symbols to distinguish 8k/16k/32k/64k/128k.
inline int mapped(int f,int exponent,int original){if(mode=="high-tiles"&&f==3){if(exponent==0)return 0;if(exponent<13)return 1;return exponent-11;}return original;}
inline int restart(int start,int end,long long step){if(mode!="restart-schedule"||step<switch_step)return (start+end)/2;return start+(end-start)/4;}
inline int aux_index(const int* board,int sym){int index=0;for(int cell:{0,1,4,5}){int r=cell/4,c=cell%4;if(sym>=4)c=3-c;for(int rot=0;rot<sym%4;++rot){int nr=c;c=3-r;r=nr;}index=index*18+board[r*4+c];}return index;}
inline unsigned long long mass(const Board& b){unsigned long long p=0;for(int v:b)if(v)p+=1ULL<<v;return p;}
inline void load_pool(){
 std::ifstream in(pool_path);if(!in)throw std::runtime_error("cannot open progress pool");std::string line;bool header=false,policy=false,train=false,after=false;std::map<unsigned long long,std::vector<Board>> grouped;
 while(std::getline(in,line)){
  if(line.rfind("# policy=",0)==0){policy=line.size()>9;continue;}if(line.rfind("# split=",0)==0){if(line!="# split=train")throw std::runtime_error("only training pool allowed");train=true;continue;}if(line=="# semantics=afterstate_exponents"){after=true;continue;}if(line.empty()||line[0]=='#')continue;
  if(!header){if(line!="c0,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15")throw std::runtime_error("invalid progress pool header");header=true;continue;}
  std::stringstream ss(line);std::string token;Board b{};for(int i=0;i<16;++i){if(!std::getline(ss,token,','))throw std::runtime_error("short pool board");size_t used=0;b[i]=std::stoi(token,&used);if(used!=token.size()||b[i]<0||b[i]>17)throw std::runtime_error("invalid pool exponent");}if(std::getline(ss,token,','))throw std::runtime_error("extra pool field");if(mass(b)==0||std::count(b.begin(),b.end(),0)==0)throw std::runtime_error("pool requires nonempty board with empty cell");grouped[mass(b)/8192].push_back(b);
 }
 if(!header||!policy||!train||!after||grouped.empty())throw std::runtime_error("pool provenance/train metadata missing");
 bands.clear();for(auto& kv:grouped)bands.push_back(std::move(kv.second));
}
template<class RNG>inline Board sample(RNG& rng){if(bands.empty())throw std::runtime_error("empty progress pool");auto& band=bands[std::uniform_int_distribution<size_t>(0,bands.size()-1)(rng)];return band[std::uniform_int_distribution<size_t>(0,band.size()-1)(rng)];}
}
#endif
