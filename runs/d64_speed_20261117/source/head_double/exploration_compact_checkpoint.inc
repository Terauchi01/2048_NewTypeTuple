#ifndef EXPLORATION_COMPACT_CHECKPOINT_INC
#define EXPLORATION_COMPACT_CHECKPOINT_INC
// Exploration-only native compact format. Call only with every worker joined.
// All four tables must be materialized; legacy dense checkpoint APIs remain intact.
#include <future>
#include "exploration_concurrency.h"
#include <array>
#include <sys/stat.h>
#include <fcntl.h>
#include <cmath>
#include <limits>
#include <fstream>
#include <filesystem>
#include <string>
namespace {
namespace exploration_compact_checkpoint {
struct Header {
  FullStateHeader model;
  uint64_t physical_count;
  uint64_t layout_hash;
  double fallback[3];
  uint64_t flag_fallback;
  uint64_t profile_path_hash;
  uint64_t header_hash;
};
struct Block { unsigned char* data; size_t bytes; };
#ifndef NT9_AOS_PROBE
std::array<Block,4> blocks() {
  return {{{reinterpret_cast<unsigned char*>(Evs.values.data()), Evs.values.size()*sizeof(compact_value_t)},
           {reinterpret_cast<unsigned char*>(Errs.values.data()), Errs.values.size()*sizeof(compact_value_t)},
           {reinterpret_cast<unsigned char*>(Aerrs.values.data()), Aerrs.values.size()*sizeof(compact_value_t)},
           {Updatecounts.values.data(), Updatecounts.values.size()}}};
}
#endif
bool allocated() {
  return Evs.values.size()==compact_layout.count && Errs.values.size()==compact_layout.count &&
      Aerrs.values.size()==compact_layout.count && Updatecounts.values.size()==compact_layout.count;
}
uint64_t layout_hash() {
  uint64_t h=fnv_bytes(FNV_OFFSET,nt9_compact::bases,sizeof(nt9_compact::bases));
  h=fnv_bytes(h,&compact_layout.digits,sizeof(compact_layout.digits));
  for(int s=0;s<2;++s) for(int f=0;f<4;++f) {
    uint64_t row[]={compact_layout.offsets[s][f],compact_layout.lengths[s][f]};
    h=fnv_bytes(h,row,sizeof(row));
  }
  return h;
}
uint64_t exploration_hash() {
  uint64_t h=current_model_hash();
  const uint64_t cfg[]={static_cast<uint64_t>(exploration::config.mode),static_cast<uint64_t>(exploration::config.formula),static_cast<uint64_t>(exploration::config.teacher),static_cast<uint64_t>(exploration::config.warmup_steps),static_cast<uint64_t>(exploration::config.decay_end_steps)};
  h=fnv_bytes(h,cfg,sizeof(cfg));h=fnv_bytes(h,&exploration::config.c,sizeof(exploration::config.c));
  const char revision[]="exploration-greedy-recomputed-nextstate-v1;TC-post-.5;epsilon1e-12;atomic-feature-stripe-v1";
  h=fnv_bytes(h,revision,sizeof(revision));
  h=fnv_bytes(h,exploration::formula_specification,strlen(exploration::formula_specification));
  const uint64_t access=static_cast<uint64_t>(exploration_concurrency::active_mode);
  h=fnv_bytes(h,&access,sizeof(access));
  return fnv_bytes(h,exploration_concurrency::concurrency_specification,strlen(exploration_concurrency::concurrency_specification));
}
Header current() {
  Header h{}; h.model=current_full_header();
  memcpy(h.model.magic,"VSECPCT2",8); h.model.version=2;
  h.model.model_hash=exploration_hash();
#ifdef NT9_FLOAT32_STORAGE
  h.model.double_size=sizeof(compact_value_t);
  const char precision[]="float32_weights_and_tc_v1";
  h.model.model_hash=fnv_bytes(h.model.model_hash,precision,sizeof(precision));
#endif
  h.physical_count=compact_layout.count; h.layout_hash=layout_hash();
  h.fallback[0]=Evs.fallback;h.fallback[1]=Errs.fallback;h.fallback[2]=Aerrs.fallback;
  h.flag_fallback=Updatecounts.fallback;
  h.profile_path_hash=fnv_bytes(FNV_OFFSET,progress_oi::profile_path.data(),progress_oi::profile_path.size());
  return h;
}
uint64_t header_hash(Header h) { h.header_hash=0;return fnv_bytes(FNV_OFFSET,&h,sizeof(h)); }
bool compatible(const Header& h) {
  Header expected=current();
  // Fallbacks are state, not configuration. Profile source path is provenance.
  expected.model.profile_path_bytes=h.model.profile_path_bytes;
  memcpy(expected.model.block_hashes,h.model.block_hashes,sizeof(h.model.block_hashes));
  memcpy(expected.fallback,h.fallback,sizeof(h.fallback));expected.flag_fallback=h.flag_fallback;
  expected.profile_path_hash=h.profile_path_hash;
  expected.header_hash=h.header_hash;
  return h.header_hash==header_hash(h) && memcmp(&h,&expected,sizeof(h))==0 &&
      h.model.profile_path_bytes<=FULL_STATE_CHUNK && h.flag_fallback<=1 &&
      std::isfinite(h.fallback[0]) && std::isfinite(h.fallback[1]) &&
      std::isfinite(h.fallback[2]) && h.fallback[2]>=0;
}
bool pread_all(int fd,void* data,size_t bytes,off_t offset) {
  auto* p=static_cast<unsigned char*>(data);
  while(bytes) { const ssize_t n=pread(fd,p,bytes,offset);if(n<0&&errno==EINTR)continue;
    if(n<=0)return false;
    p+=n;bytes-=size_t(n);offset+=n; }
  return true;
}
// Four independent FNV streams; at most 4 MiB scratch memory on load.
bool verify_block(int fd,off_t offset,size_t bytes,uint64_t expected,bool flags,bool absolute=false) {
  std::vector<unsigned char> scratch(FULL_STATE_CHUNK);uint64_t hash=FNV_OFFSET;
  while(bytes) {size_t n=std::min(bytes,scratch.size());
    if(!pread_all(fd,scratch.data(),n,offset))return false;
    if(flags) for(size_t i=0;i<n;++i)if(scratch[i]>1)return false;
    if(!flags) for(size_t i=0;i<n;i+=sizeof(compact_value_t)) {
      compact_value_t value;memcpy(&value,scratch.data()+i,sizeof(value));
      if(!std::isfinite(value)||(absolute&&value<0))return false;
    }
    hash=fnv_bytes(hash,scratch.data(),n);bytes-=n;offset+=n;
  }
  return hash==expected;
}
}
} // anonymous namespace
bool save_exploration_compact_state(const char* filename) {
  using namespace exploration_compact_checkpoint;
  if(!allocated()) {fprintf(stderr,"compact native save requires all tables materialized\n");return false;}
  Header h=current();if(h.model.profile_path_bytes>FULL_STATE_CHUNK)return false;
#ifdef NT9_AOS_PROBE
  if(!std::isfinite(h.fallback[0])||!std::isfinite(h.fallback[1])||!std::isfinite(h.fallback[2])||h.fallback[2]<0||h.flag_fallback>1)return false;
  for(size_t i=0;i<compact_layout.count;++i)
    if(!std::isfinite(Evs.values[i])||!std::isfinite(Errs.values[i])||!std::isfinite(Aerrs.values[i])||Aerrs.values[i]<0||Updatecounts.values[i]>1)return false;
  // Serialize the legacy SoA logical block order without a full-size copy.
  const size_t chunk_items=FULL_STATE_CHUNK/sizeof(compact_value_t);
  std::vector<compact_value_t> weights(chunk_items);
  std::vector<compact_value_t> doubles(chunk_items);
  std::vector<uint8_t> flags(chunk_items);
  auto gather=[&](size_t field,size_t start,size_t n)->const unsigned char* {
    if(field==3) {for(size_t j=0;j<n;++j)flags[j]=Updatecounts.values[start+j];return flags.data();}
    if(field==0) {for(size_t j=0;j<n;++j)weights[j]=Evs.values[start+j];return reinterpret_cast<const unsigned char*>(weights.data());}
    for(size_t j=0;j<n;++j) {
      if(field==1)doubles[j]=Errs.values[start+j];
      else doubles[j]=Aerrs.values[start+j];
    }
    return reinterpret_cast<const unsigned char*>(doubles.data());
  };
  for(size_t field=0;field<4;++field) {
    uint64_t hash=FNV_OFFSET;const size_t width=field==3?1:(field==0?sizeof(compact_value_t):sizeof(compact_value_t));
    for(size_t start=0;start<compact_layout.count;) {
      const size_t n=std::min(chunk_items,compact_layout.count-start);
      hash=fnv_bytes(hash,gather(field,start,n),n*width);start+=n;
    }
    h.model.block_hashes[field]=hash;
  }
#else
  auto data=blocks();std::array<std::future<uint64_t>,4> hashes;
  for(size_t i=0;i<4;++i)hashes[i]=std::async(std::launch::async,[b=data[i]] {
    return fnv_bytes(FNV_OFFSET,b.data,b.bytes);});
  for(size_t i=0;i<4;++i)h.model.block_hashes[i]=hashes[i].get();
#endif
  h.header_hash=header_hash(h);
  std::string temporary=std::string(filename)+".tmp.XXXXXX";
  int fd=mkstemp(temporary.data());if(fd<0)return false;
  FILE* fp=fdopen(fd,"wb");if(!fp){close(fd);remove(temporary.c_str());return false;}
  bool ok=write_block(fp,&h,sizeof(h),1,"compact header") &&
      write_block(fp,progress_oi::profile_path.data(),1,h.model.profile_path_bytes,"profile path");
#ifdef NT9_AOS_PROBE
  for(size_t field=0;ok&&field<4;++field) {
    const size_t width=field==3?1:(field==0?sizeof(compact_value_t):sizeof(compact_value_t));
    for(size_t start=0;ok&&start<compact_layout.count;) {
      const size_t n=std::min(chunk_items,compact_layout.count-start);
      ok=write_block(fp,gather(field,start,n),1,n*width,"compact physical block");start+=n;
    }
  }
#else
  for(auto b:data)if(ok)ok=write_block(fp,b.data,1,b.bytes,"compact physical block");
#endif
  if(ok)ok=fflush(fp)==0&&fsync(fd)==0;
  if(fclose(fp)!=0)ok=false;
#ifdef NT9_AOS_PROBE
  // Native API never replaces an existing target. The temp resides in the
  // destination directory, so link is a same-filesystem atomic no-replace.
  if(ok)ok=link(temporary.c_str(),filename)==0;
  if(ok)ok=unlink(temporary.c_str())==0;
#else
  if(ok)ok=rename(temporary.c_str(),filename)==0;
#endif
  if(ok) {std::string path(filename);auto slash=path.find_last_of('/');
    std::string directory=slash==std::string::npos?".":(slash==0?"/":path.substr(0,slash));
    int dirfd=open(directory.c_str(),O_RDONLY|O_DIRECTORY);
    if(dirfd<0)ok=false;else {if(fsync(dirfd)!=0)ok=false;close(dirfd);}}
  if(!ok)remove(temporary.c_str());
  return ok;
}
#ifdef NT9_AOS_PROBE
// Fault injection is compiled only into independent tests.
#ifdef NT9_AOS_TEST_FAULT
int aos_test_fail_after_field=-1;
#endif
// Snapshot-based guard; it does not reserve RAM against other processes.
// The 12 GiB margin is deliberately omitted for tiny test fixtures only.
static bool aos_stage_memory_available(uint64_t required) {
#ifdef __linux__
  std::ifstream mem("/proc/meminfo");std::string key,unit;uint64_t value=0,available=0;
  while(mem>>key>>value>>unit)if(key=="MemAvailable:"){available=value*1024;break;}
  if(!available)return false;
  // Respect finite cgroup memory limits at each ancestor (v2 or v1 memory
  // controller). Unavailable pseudo-files mean that controller is inactive.
  std::ifstream groups("/proc/self/cgroup");if(!groups)return false;std::string line;
  while(std::getline(groups,line)){
    auto first=line.find(':');auto second=first==std::string::npos?first:line.find(':',first+1);
    if(first==std::string::npos||second==std::string::npos)continue;
    const std::string controllers=line.substr(first+1,second-first-1),rel=line.substr(second+1);
    const bool v2=controllers.empty();
    const bool v1=(","+controllers+",").find(",memory,")!=std::string::npos;
    if(!v2&&!v1)continue;
    if(rel.empty()||rel[0]!='/'||rel.find("..")!=std::string::npos)return false;
    const std::filesystem::path root=v2?"/sys/fs/cgroup":"/sys/fs/cgroup/memory";
    std::filesystem::path current=root/rel.substr(1);
    for(;;){
      std::ifstream maxfile(current/(v2?"memory.max":"memory.limit_in_bytes"));
      std::ifstream usedfile(current/(v2?"memory.current":"memory.usage_in_bytes"));
      std::string limit;uint64_t used=0;
      if(maxfile>>limit && usedfile>>used && limit!="max"){
        try {size_t consumed=0;const uint64_t cap=std::stoull(limit,&consumed);if(consumed!=limit.size())return false;available=std::min(available,cap>used?cap-used:0);}
        catch(...){return false;}
      }
      if(current==root)break;
      auto parent=current.parent_path();if(parent==current||current.string().rfind(root.string()+"/",0)!=0)return false;current=parent;
    }
    break;
  }
  constexpr uint64_t margin=UINT64_C(12)*1024*1024*1024;
  const uint64_t reserve=required>=UINT64_C(1024)*1024*1024?margin:0;
  return required<=UINT64_MAX-reserve && available>=required+reserve;
#else
  (void)required;return false;
#endif
}
// Caller must quiesce all workers before loading; all fields must be fully
// allocated or completely unallocated. Partial live models are rejected.
// Buffer all fields and verify twice-observed file metadata before modifying any
// live AoS entries. Full NT9 float32 uses 13 bytes/entry of scratch.
bool load_exploration_compact_state(const char* filename) {
  using namespace exploration_compact_checkpoint;
  FILE* fp=fopen(filename,"rb"); if(!fp)return false;
  bool ok=false;
  try {
    Header h{};struct stat before{},after{};
    const bool had_model=allocated();
    const bool any_model=Evs.allocated()||Errs.allocated()||Aerrs.allocated()||Updatecounts.allocated();
    auto exact_fallback=[](double x) {
      if (!std::isfinite(x) || std::abs(x)>std::numeric_limits<compact_value_t>::max()) return false;
      const compact_value_t y=static_cast<compact_value_t>(x);
      return static_cast<double>(y)==x && std::signbit(y)==std::signbit(x);
    };
    ok=(had_model||!any_model) && fstat(fileno(fp),&before)==0 && S_ISREG(before.st_mode) &&
       read_block(fp,&h,sizeof(h),1,"compact header") && compatible(h) &&
       exact_fallback(h.fallback[0]) && exact_fallback(h.fallback[1]) &&
       exact_fallback(h.fallback[2]) && h.fallback[2]>=0;
    const uint64_t payload=sizeof(h)+h.model.profile_path_bytes;
    const uint64_t expected=payload+uint64_t(compact_layout.count)*(3*sizeof(compact_value_t)+1);
    if(ok)ok=before.st_size>=0&&uint64_t(before.st_size)==expected;
    std::vector<unsigned char> path;
    if(ok){path.resize(h.model.profile_path_bytes);ok=pread_all(fileno(fp),path.data(),path.size(),sizeof(h)) && fnv_bytes(FNV_OFFSET,path.data(),path.size())==h.profile_path_hash;}
    std::vector<compact_value_t> weights;
    std::vector<compact_value_t> errors,absolutes;
    std::vector<uint8_t> flags;
    if(ok){
      const uint64_t scratch=uint64_t(compact_layout.count)*(3*sizeof(compact_value_t)+1);
      const uint64_t model=(!had_model&&nt9_compact::aos_entries.empty())?uint64_t(compact_layout.count)*sizeof(nt9_compact::AoSEntry):0;
      if(!aos_stage_memory_available(scratch+model)){fprintf(stderr,"compact staged load: insufficient available RAM/cgroup headroom for %llu bytes plus safety margin\n",(unsigned long long)(scratch+model));ok=false;}
    }
    if(ok){weights.resize(compact_layout.count);errors.resize(compact_layout.count);absolutes.resize(compact_layout.count);flags.resize(compact_layout.count);}
    // Each field's offset is advanced explicitly; no shared-offset IO races.
    off_t pos=static_cast<off_t>(payload);
    auto read_field=[&](size_t field,void* data,size_t bytes){
      if(!ok)return;uint64_t hash=FNV_OFFSET;auto* p=static_cast<unsigned char*>(data);
      for(size_t done=0;ok&&done<bytes;){const size_t n=std::min(size_t(FULL_STATE_CHUNK),bytes-done);
        ok=pread_all(fileno(fp),p+done,n,pos+static_cast<off_t>(done));
        if(ok)hash=fnv_bytes(hash,p+done,n);done+=n;}
      pos+=static_cast<off_t>(bytes);if(ok)ok=hash==h.model.block_hashes[field];
#ifdef NT9_AOS_TEST_FAULT
      if(ok&&aos_test_fail_after_field==int(field))ok=false;
#endif
    };
    if(ok){
      read_field(0,weights.data(),weights.size()*sizeof(compact_value_t));
      read_field(1,errors.data(),errors.size()*sizeof(compact_value_t));
      read_field(2,absolutes.data(),absolutes.size()*sizeof(compact_value_t));
      read_field(3,flags.data(),flags.size());
    }
    if(ok)for(size_t i=0;i<weights.size();++i)if(!std::isfinite(weights[i])||!std::isfinite(errors[i])||!std::isfinite(absolutes[i])||absolutes[i]<0||flags[i]>1){ok=false;break;}
    if(ok)ok=fstat(fileno(fp),&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_size==after.st_size && before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec && before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec;
    if(fclose(fp)!=0)ok=false;fp=nullptr;
    if(ok){
      // Existing model is left untouched until validation completes. An
      // uninitialized driver needs storage before the no-throw commit.
      if(!had_model){Evs.allocate();Errs.allocate();Aerrs.allocate();Updatecounts.allocate();}
      // No allocation or I/O during commit; workers remain stopped.
      for(size_t i=0;i<weights.size();++i){Evs.values[i]=weights[i];Errs.values[i]=errors[i];Aerrs.values[i]=absolutes[i];Updatecounts.values[i]=flags[i];}
      Evs.fallback=static_cast<compact_value_t>(h.fallback[0]);Errs.fallback=h.fallback[1];Aerrs.fallback=h.fallback[2];Updatecounts.fallback=static_cast<uint8_t>(h.flag_fallback);
      compact_statistics_ready.store(true,std::memory_order_release);
    }
  } catch(const std::exception& e){fprintf(stderr,"compact staged load failed: %s\n",e.what());ok=false;}
  if(fp)fclose(fp);
  if(!ok)fprintf(stderr,"invalid or unreadable native compact checkpoint: %s\n",filename);
  return ok;
}
#else
bool load_exploration_compact_state(const char* filename) {
  using namespace exploration_compact_checkpoint;
  FILE* fp=fopen(filename,"rb");if(!fp)return false;
  Header h{};bool ok=read_block(fp,&h,sizeof(h),1,"compact header")&&compatible(h);
  const uint64_t payload=sizeof(h)+h.model.profile_path_bytes;
  const uint64_t expected=payload+uint64_t(compact_layout.count)*(3*sizeof(compact_value_t)+1);
  struct stat st{};if(ok)ok=fstat(fileno(fp),&st)==0&&st.st_size>=0&&uint64_t(st.st_size)==expected;
  if(ok) {std::vector<unsigned char> path(h.model.profile_path_bytes);
    ok=pread_all(fileno(fp),path.data(),path.size(),sizeof(h)) &&
        fnv_bytes(FNV_OFFSET,path.data(),path.size())==h.profile_path_hash;}
  // Check all blocks before allocating or changing any model state.
  if(ok) {
    std::array<std::future<bool>,4> checks;off_t offset=static_cast<off_t>(payload);
    for(size_t i=0;i<4;++i) {size_t bytes=compact_layout.count*(i==3?1:(i==0?sizeof(compact_value_t):sizeof(compact_value_t)));
      checks[i]=std::async(std::launch::async,[fd=fileno(fp),offset,bytes,hash=h.model.block_hashes[i],i] {
        return verify_block(fd,offset,bytes,hash,i==3,i==2);});offset+=bytes;}
    for(auto& check:checks)if(!check.get())ok=false;
  }
  if(ok) {
    Evs.allocate();Errs.allocate();Aerrs.allocate();Updatecounts.allocate();
    ok=fseeko(fp,static_cast<off_t>(payload),SEEK_SET)==0;
#ifdef NT9_AOS_PROBE
    const size_t chunk_items=FULL_STATE_CHUNK/sizeof(compact_value_t);
    std::vector<compact_value_t> weights(chunk_items);
    std::vector<compact_value_t> doubles(chunk_items);
    std::vector<uint8_t> flags(chunk_items);
    for(size_t field=0;ok&&field<4;++field) {
      const size_t width=field==3?1:(field==0?sizeof(compact_value_t):sizeof(compact_value_t));
      for(size_t start=0;ok&&start<compact_layout.count;) {
        const size_t n=std::min(chunk_items,compact_layout.count-start);
        void* buffer=field==3?static_cast<void*>(flags.data()):field==0?static_cast<void*>(weights.data()):static_cast<void*>(doubles.data());
        ok=read_block(fp,buffer,1,n*width,"compact physical block");
        if(ok)for(size_t j=0;j<n;++j) {
          if(field==0)Evs.values[start+j]=weights[j];
          else if(field==1)Errs.values[start+j]=doubles[j];
          else if(field==2)Aerrs.values[start+j]=doubles[j];
          else Updatecounts.values[start+j]=flags[j];
        }
        start+=n;
      }
    }
#else
    for(auto b:blocks())if(ok)ok=read_block(fp,b.data,1,b.bytes,"compact physical block");
#endif
    if(ok) {Evs.fallback=h.fallback[0];Errs.fallback=h.fallback[1];Aerrs.fallback=h.fallback[2];
      Updatecounts.fallback=static_cast<uint8_t>(h.flag_fallback);
      compact_statistics_ready.store(true,std::memory_order_release);}
  }
  if(fclose(fp)!=0)ok=false;
  if(!ok)fprintf(stderr,"invalid or unreadable native compact checkpoint: %s\n",filename);
  return ok;
}

#endif // NT9_AOS_PROBE

#endif

