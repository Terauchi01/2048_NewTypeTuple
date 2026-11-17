#ifndef NT9_THP_ADVICE_H
#define NT9_THP_ADVICE_H
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace nt9_thp {
struct Range{std::uintptr_t begin=0;std::size_t bytes=0;};
// Only pages wholly contained in this allocation's used byte range are advised.
inline Range inner_pages(std::uintptr_t begin,std::size_t bytes,std::size_t page){
 if(!page||bytes>std::numeric_limits<std::uintptr_t>::max()-begin)return {};
 const auto end=begin+bytes;const auto offset=begin%page;
 const std::size_t skip=offset?page-offset:0;
 if(skip>bytes)return {};
 begin+=skip;const auto aligned_end=end-end%page;
 return aligned_end>begin?Range{begin,static_cast<std::size_t>(aligned_end-begin)}:Range{};
}
using Advice=int(*)(void*,std::size_t,int);
inline int system_advice(void* p,std::size_t bytes,int flag){
#ifdef __linux__
 return ::madvise(p,bytes,flag);
#else
 (void)p;(void)bytes;(void)flag;errno=ENOSYS;return -1;
#endif
}
inline int hugepage_flag(){
#ifdef __linux__
 return MADV_HUGEPAGE;
#else
 return 0;
#endif
}
struct Result{Range range{};const char*status="disabled";int error=0;};
inline Result request(void* ptr,std::size_t bytes,std::size_t page,bool enabled,Advice advice=system_advice){
 Result r;
 if(!enabled)return r;
 r.range=inner_pages(reinterpret_cast<std::uintptr_t>(ptr),bytes,page);
 if(!r.range.bytes){r.status="no_full_page";return r;}
 if(advice(reinterpret_cast<void*>(r.range.begin),r.range.bytes,hugepage_flag())==0){r.status="success";return r;}
 r.status="failed_fallback";r.error=errno;return r;
}
inline bool runtime_enabled(){
 const char* value=std::getenv("NT9_THP");
 if(value&&std::strcmp(value,"0")&&std::strcmp(value,"1"))throw std::runtime_error("NT9_THP must be 0 or 1");
#ifdef __linux__
 return !value||value[0]=='1';
#else
 return false;
#endif
}
#ifdef NT9_THP_UNIT_TEST
inline Advice unit_advice=system_advice;
#endif
inline void advise_allocation(void* ptr,std::size_t bytes,std::size_t element_bytes,std::size_t count){
 std::size_t page=0;
#ifdef __linux__
 const long parsed=::sysconf(_SC_PAGESIZE);if(parsed>0)page=static_cast<std::size_t>(parsed);
#endif
 Advice advice=system_advice;
#ifdef NT9_THP_UNIT_TEST
 advice=unit_advice;
#endif
 const auto r=request(ptr,bytes,page,runtime_enabled(),advice);
 std::printf("thp_allocation,element_bytes,%zu,count,%zu,requested_bytes,%zu,page_bytes,%zu,advised_bytes,%zu,status,%s,errno,%d\n",element_bytes,count,bytes,page,r.range.bytes,r.status,r.error);
 std::fflush(stdout);
}
}
#endif
