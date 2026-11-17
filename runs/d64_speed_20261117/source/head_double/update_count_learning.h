#ifndef UPDATE_COUNT_LEARNING_H
#define UPDATE_COUNT_LEARNING_H
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
namespace count_learning {
enum class Mode {tc, inverse_count};
inline Mode active_mode=Mode::tc;
inline double exponent=0.5;
// Legacy Updatecounts stores only 0/1. New encoding retains that export mask in
// bit0 and uses bits1..30 for scalar updates since the first count-study fork.
inline bool encoded=true;
inline Mode parse(const std::string& s) {
 if(s=="tc")return Mode::tc;
 if(s=="inverse-count")return Mode::inverse_count;
 throw std::invalid_argument("count rule must be tc or inverse-count");
}
inline const char* name(Mode mode) {
 switch(mode){case Mode::tc:return "tc";case Mode::inverse_count:return "inverse-count";}
 throw std::invalid_argument("invalid count rule");
}
inline void validate(){if(!std::isfinite(exponent)||exponent<=0||exponent>1)throw std::invalid_argument("count exponent must be in (0,1]");}
inline unsigned count(int packed) {if(packed<0)throw std::invalid_argument("negative packed update count");return static_cast<unsigned>(packed)>>1;}
inline int increment(int packed){const unsigned maximum=static_cast<unsigned>(std::numeric_limits<int>::max())>>1;return static_cast<int>((std::min(count(packed)+1,maximum)<<1)|1u);}
static_assert(alignof(int) >= std::atomic_ref<int>::required_alignment);
inline int load(int& packed){return std::atomic_ref<int>(packed).load(std::memory_order_relaxed);}
inline unsigned acquire_prior_count(int& packed){
 std::atomic_ref<int> counter(packed);int observed=counter.load(std::memory_order_relaxed);
 for(;;){const int next=increment(observed);if(counter.compare_exchange_weak(observed,next,std::memory_order_relaxed))return count(observed);}
}
struct StageStats {std::uint64_t scalar_updates=0, zero_count=0, max_prior_count=0;long double count_sum=0, alpha_sum=0;};
struct ReferenceStats {std::uint64_t references=0, zero_count=0, max_count=0;long double count_sum=0;};
inline thread_local std::array<StageStats,2> local_stats{};
inline thread_local std::array<ReferenceStats,2> local_references{};
inline std::mutex stats_mutex;
inline std::vector<std::array<StageStats,2>> worker_stats;
inline std::vector<std::array<ReferenceStats,2>> worker_references;
inline void record_reference(int& packed,int stage){auto& s=local_references.at(stage);const auto n=count(load(packed));++s.references;s.zero_count+=n==0;s.max_count=std::max(s.max_count,static_cast<std::uint64_t>(n));s.count_sum+=n;}
inline void publish(int worker){std::lock_guard<std::mutex> lock(stats_mutex);if(worker_stats.size()<=static_cast<unsigned>(worker)){worker_stats.resize(worker+1);worker_references.resize(worker+1);}worker_stats[worker]=local_stats;worker_references[worker]=local_references;}
inline void print_stats(){for(unsigned w=0;w<worker_stats.size();++w)for(unsigned st=0;st<2;++st){const auto& s=worker_stats[w][st];std::printf("count_diagnostics,worker,%u,stage,%u,scalar_updates,%llu,zero_prior_count,%llu,max_prior_count,%llu,mean_prior_count,%.9Lf,mean_effective_alpha,%.12Lf\n",w,st,(unsigned long long)s.scalar_updates,(unsigned long long)s.zero_count,(unsigned long long)s.max_prior_count,s.scalar_updates?s.count_sum/s.scalar_updates:0,s.scalar_updates?s.alpha_sum/s.scalar_updates:0);}}
inline void print_reference_stats(){for(unsigned w=0;w<worker_references.size();++w)for(unsigned st=0;st<2;++st){const auto& s=worker_references[w][st];std::printf("count_references,worker,%u,stage,%u,references,%llu,zero_update_count,%llu,max_update_count,%llu,mean_update_count,%.9Lf\n",w,st,(unsigned long long)s.references,(unsigned long long)s.zero_count,(unsigned long long)s.max_count,s.references?s.count_sum/s.references:0);}}
inline void update(double& weight,double& errs,double& aerrs,double delta,int& packed,int stage){
 const auto n=acquire_prior_count(packed);
 errs+=delta;aerrs+=std::fabs(delta);
 const double alpha=active_mode==Mode::tc ? .5*std::fabs(errs)/aerrs : .5/std::pow(1.0+n,exponent);
 // Preserve the existing TC operation order for exact control equivalence.
 if(active_mode==Mode::tc)weight+=delta*.5*std::fabs(errs)/aerrs;
 else weight+=delta*alpha;
 auto& s=local_stats.at(stage);++s.scalar_updates;s.zero_count+=n==0;s.max_prior_count=std::max(s.max_prior_count,static_cast<std::uint64_t>(n));s.count_sum+=n;s.alpha_sum+=alpha;
}
}
#endif
