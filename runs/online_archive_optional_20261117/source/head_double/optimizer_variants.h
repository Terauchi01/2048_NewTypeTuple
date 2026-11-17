#ifndef OPTIMIZER_VARIANTS_H
#define OPTIMIZER_VARIANTS_H
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>
#include <thread>
#include "learning_rule_schedule.h"
namespace optimizer_variants {
enum class Mode { control, progress, lazy, cap, decay };
inline Mode mode=Mode::control;
inline bool read_only_evaluation=false;
inline Mode parse(const std::string& s){if(s=="control")return Mode::control;if(s=="progress")return Mode::progress;if(s=="lazy")return Mode::lazy;if(s=="cap")return Mode::cap;if(s=="decay")return Mode::decay;throw std::invalid_argument("optimizer variant must be control|progress|lazy|cap|decay");}
inline const char* name(Mode m){switch(m){case Mode::control:return "control";case Mode::progress:return "progress";case Mode::lazy:return "lazy";case Mode::cap:return "cap";case Mode::decay:return "decay";}throw std::invalid_argument("invalid optimizer variant");}
// 0: unseen; -1: initialization in progress; -2: initialized, never updated;
// 1: updated. Existing export mask regards only >=1 as learned.
inline void ensure_initialized(double& w,int& flag,double value){
 std::atomic_ref<int> a(flag);int state=a.load(std::memory_order_acquire);
 if(state==0){int expected=0;if(a.compare_exchange_strong(expected,-1,std::memory_order_acq_rel)){w=value;a.store(-2,std::memory_order_release);return;}state=expected;}
 while(state==-1){std::this_thread::yield();state=a.load(std::memory_order_acquire);}
}
inline double lazy_value(double& w,int& flag,double initial){
 if(read_only_evaluation){const int state=std::atomic_ref<int>(flag).load(std::memory_order_acquire);if(state==-1)throw std::runtime_error("evaluation encountered in-flight initialization");return state==0?initial:w;}
 ensure_initialized(w,flag,initial);return w;
}
inline int load_flag(int& flag){return std::atomic_ref<int>(flag).load(std::memory_order_acquire);}
inline void mark_updated(int& flag){std::atomic_ref<int>(flag).store(1,std::memory_order_release);}
inline void update(double& w,double& e,double& a,double delta,learning_rule::Rule rule){
 if(mode!=Mode::cap){learning_rule::update(w,e,a,delta,rule);return;}
 if(rule==learning_rule::Rule::tc){a+=std::fabs(delta);e+=delta;w+=delta*std::min(.1,.5*std::fabs(e)/a);}else w+=delta*.1;
}
inline void reset(double& e,double& a){if(mode==Mode::decay){e*=.5;a=std::max(1e-12,a*.5);}else{e=1e-12;a=1e-12;}}
}
#endif
