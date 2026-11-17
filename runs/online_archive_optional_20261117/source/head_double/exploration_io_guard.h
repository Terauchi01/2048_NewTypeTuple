#ifndef EXPLORATION_IO_GUARD_H
#define EXPLORATION_IO_GUARD_H
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace exploration_io {
namespace fs=std::filesystem;
struct Owner {std::string host,nonce,boot_id;long long pid=0;unsigned long long starttick=0;};
inline std::string current_boot_id() {
  std::ifstream f("/proc/sys/kernel/random/boot_id");std::string id,extra;
  if(!(f>>id)||f>>extra||id.size()!=36)throw std::runtime_error("cannot identify IO lock host boot");
  for(std::size_t i=0;i<id.size();++i) {
    const char c=id[i];
    if(i==8||i==13||i==18||i==23) {if(c!='-')throw std::runtime_error("invalid IO lock boot id");}
    else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))throw std::runtime_error("invalid IO lock boot id");
  }
  return id;
}
inline std::string hostname() {
  char name[256]{};if(gethostname(name,sizeof(name)-1))throw std::runtime_error("cannot identify IO lock host");
  return name;
}
inline unsigned long long starttick(long long pid) {
  std::ifstream f("/proc/"+std::to_string(pid)+"/stat");std::string text,field;std::getline(f,text);
  auto end=text.rfind(')');if(end==std::string::npos)return 0;
  std::istringstream s(text.substr(end+2));
  for(int i=0;i<20;++i)if(!(s>>field))return 0;
  try{return std::stoull(field);}catch(...){return 0;}
}
inline bool read_owner(const fs::path& dir,Owner& owner) {
  std::ifstream f(dir/"owner.txt");std::string magic;
  if(!(f>>magic>>std::quoted(owner.host)>>owner.pid>>owner.starttick>>std::quoted(owner.nonce)))return false;
  owner.boot_id.clear();
  if(magic=="EXPLORATION_IO_LOCK_V2") {if(!(f>>std::quoted(owner.boot_id)))return false;}
  else if(magic!="EXPLORATION_IO_LOCK_V1")return false;
  f>>std::ws;return f.eof()&&owner.pid>0&&owner.starttick>0&&!owner.nonce.empty();
}
inline void sync(const fs::path& path,bool directory=false) {
  int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|(directory?O_DIRECTORY:0));
  if(fd<0)throw std::runtime_error("IO lock fsync open failed: "+path.string());
  int rc=fsync(fd);close(fd);if(rc)throw std::runtime_error("IO lock fsync failed: "+path.string());
}
class Guard {
  fs::path directory_,temporary_;
  Owner mine_;
  bool owns_=false,published_=false;
  double wait_=0;
public:
  Guard(const fs::path& directory,long long timeout_seconds):directory_(directory) {
    if(directory_.empty()||timeout_seconds<=0)throw std::invalid_argument("IO guard requires path and finite positive timeout");
    directory_=fs::absolute(directory_).lexically_normal();
    fs::create_directories(directory_.parent_path());
    mine_.host=hostname();mine_.pid=getpid();mine_.starttick=starttick(mine_.pid);mine_.boot_id=current_boot_id();
    if(!mine_.starttick)throw std::runtime_error("cannot identify IO lock process starttick");
    const auto begin=std::chrono::steady_clock::now();
    mine_.nonce=std::to_string(mine_.pid)+"-"+std::to_string(mine_.starttick)+"-"+std::to_string(begin.time_since_epoch().count());
    temporary_=directory_/("owner.tmp-"+mine_.nonce);
    for(;;) {
      if(::mkdir(directory_.c_str(),0700)==0){owns_=true;break;}
      const int error=errno;
      if(error!=EEXIST)throw std::runtime_error("IO lock mkdir failed: "+directory_.string()+": "+strerror(error));
      const auto type=fs::symlink_status(directory_);
      // A holder can finish between mkdir(EEXIST) and this status lookup.
      if(!fs::exists(type))continue;
      if(!fs::is_directory(type)||fs::is_symlink(type))throw std::runtime_error("invalid IO lock token; refusing recovery: "+directory_.string());
      // A local holder can die immediately before its controller verifies
      // and quarantines the token. Keep waiting rather than failing during
      // that recovery window. The Guard never removes another owner's token;
      // a missing controller therefore still ends in the bounded timeout.
      wait_=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
      if(wait_>=timeout_seconds)throw std::runtime_error("IO lock timeout; existing token left intact: "+directory_.string());
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    wait_=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    try {
      std::ofstream out(temporary_);
      out<<"EXPLORATION_IO_LOCK_V2 "<<std::quoted(mine_.host)<<' '<<mine_.pid<<' '<<mine_.starttick<<' '<<std::quoted(mine_.nonce)<<' '<<std::quoted(mine_.boot_id)<<'\n';
      out.close();if(!out)throw std::runtime_error("cannot publish IO lock owner");
      sync(temporary_);fs::rename(temporary_,directory_/"owner.txt");published_=true;sync(directory_,true);
    } catch(...) {try{release();}catch(...){std::fprintf(stderr,"IO lock initialization failed; token may require inspection: %s\n",directory_.c_str());}throw;}
  }
  Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
  double wait_seconds() const{return wait_;}
  void release() {
    if(!owns_)return;
    for(const auto& entry:fs::directory_iterator(directory_)) {
      if(entry.path().filename()!="owner.txt"&&entry.path()!=temporary_)
        throw std::runtime_error("unexpected IO token contents; owner left intact: "+directory_.string());
    }
    if(published_) {
      Owner owner;
      if(!read_owner(directory_,owner)||owner.host!=mine_.host||owner.pid!=mine_.pid||owner.starttick!=mine_.starttick||owner.nonce!=mine_.nonce||owner.boot_id!=mine_.boot_id)
        throw std::runtime_error("IO lock ownership changed; token left intact: "+directory_.string());
      fs::remove(directory_/"owner.txt");published_=false;
    }
    fs::remove(temporary_);
    // Never recursively remove a token. Unexpected files keep it fail-closed.
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(::rmdir(directory_.c_str())!=0) {
      const int error=errno;
      // NFS can briefly retain an unlinked owner as .nfs* while a waiter
      // closes its read handle. Wait for that cleanup; never delete contents.
      if((error!=ENOTEMPTY&&error!=EEXIST&&error!=EBUSY)||std::chrono::steady_clock::now()>=until)
        throw std::runtime_error("cannot release IO lock token: "+directory_.string());
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    owns_=false;sync(directory_.parent_path(),true);
  }
  ~Guard(){try{release();}catch(const std::exception& e){std::fprintf(stderr,"IO guard release: %s\n",e.what());}}
};
}
#endif
