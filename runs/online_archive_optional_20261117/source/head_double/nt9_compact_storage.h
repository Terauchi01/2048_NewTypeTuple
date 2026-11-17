#ifndef NT9_COMPACT_STORAGE_H
#define NT9_COMPACT_STORAGE_H
#include "nt9_compact_layout.h"
#include "nt9_thp_advice.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <type_traits>
#include <atomic>
#include <cstddef>

namespace nt9_compact {
// Native arithmetic uses compact row indices. Only checkpoint IO translates
// legacy base-7 indices. Unreachable entries have a canonical fallback value.
#ifdef NT9_AOS_PROBE
// Benchmark-only storage. Logical fields retain genuine lvalue references so
// existing atomic_ref reads/stores and feature-stripe locking remain unchanged.
struct AoSEntry { float weight, error, absolute; uint8_t mask; };
static_assert(sizeof(AoSEntry)==16 && alignof(AoSEntry)>=alignof(float));
static_assert(std::atomic_ref<float>::is_always_lock_free && std::atomic_ref<uint8_t>::is_always_lock_free);
static_assert(offsetof(AoSEntry,weight)%std::atomic_ref<float>::required_alignment==0);
static_assert(offsetof(AoSEntry,error)%std::atomic_ref<float>::required_alignment==0);
static_assert(offsetof(AoSEntry,absolute)%std::atomic_ref<float>::required_alignment==0);
static_assert(sizeof(AoSEntry)%std::atomic_ref<float>::required_alignment==0);
inline std::vector<AoSEntry> aos_entries;
template<class T,int Slot=0> struct Table {
  const Layout& layout;
  T fallback{};
  bool materialized=false;
  struct Values {
    Table* table;
    T& operator[](std::size_t i) const {
      if constexpr(std::is_same_v<T,uint8_t>) return aos_entries[i].mask;
      else {
        if constexpr(Slot==0)return aos_entries[i].weight;
        else if constexpr(Slot==1)return aos_entries[i].error;
        else return aos_entries[i].absolute;
      }
    }
    bool empty() const {return !table->materialized;}
    std::size_t size() const {return table->materialized?table->layout.count:0;}
  } values{this};
  explicit Table(const Layout& l):layout(l){}
  bool allocated() const {return materialized;}
  void allocate(){
    if(materialized)return;
    if(aos_entries.empty()) {
      aos_entries.reserve(layout.count);
      nt9_thp::advise_allocation(aos_entries.data(),layout.count*sizeof(AoSEntry),sizeof(AoSEntry),layout.count);
      aos_entries.resize(layout.count);
    }
    if(aos_entries.size()!=layout.count)throw std::runtime_error("AoS layout mismatch");
    for(std::size_t i=0;i<layout.count;++i)values[i]=fallback;
    materialized=true;
  }
  void fill(T value,bool materialize=true){
    fallback=value;
    if(materialize)allocate();
    if(allocated())for(std::size_t i=0;i<layout.count;++i)values[i]=value;
  }
  struct Row {Table& table;std::size_t offset;T& operator[](std::size_t i){return table.values[offset+i];}};
  struct Filter {Table& table;int s,f;Row operator[](int t){return {table,table.layout.row(s,f,t)};}};
  struct Stage {Table& table;int s;Filter operator[](int f){return {table,s,f};}};
  Stage operator[](int s){return {*this,s};}
  bool locate(std::size_t logical,std::size_t& physical)const {
    const auto a=logical%layout.dense_length;logical/=layout.dense_length;
    const int t=logical%7,f=(logical/7)%4,s=logical/28;std::size_t compact;
    if(s>=2||!layout.compact_index(s,f,a,compact))return false;
    physical=layout.row(s,f,t)+compact;return true;
  }
  template<class Disk>void export_chunk(std::size_t start,Disk*out,std::size_t n)const{
    for(std::size_t i=0;i<n;++i){std::size_t physical;
      out[i]=allocated()&&locate(start+i,physical)?static_cast<Disk>(values[physical]):static_cast<Disk>(fallback);}
  }
  template<class Disk>void import_chunk(std::size_t start,const Disk*in,std::size_t n){
    for(std::size_t i=0;i<n;++i){std::size_t physical;if(!locate(start+i,physical))continue;
      const T value=static_cast<T>(in[i]);if(!allocated()&&std::memcmp(&value,&fallback,sizeof(T))==0)continue;
      allocate();values[physical]=value;}
  }
};
#else
template<class T> struct Table {
  const Layout& layout;
  std::vector<T> values;
  T fallback{};
  explicit Table(const Layout& l) : layout(l) {}
  bool allocated() const { return !values.empty(); }
  void allocate() {
    if (!allocated()) {
      values.reserve(layout.count);
      nt9_thp::advise_allocation(values.data(), layout.count * sizeof(T), sizeof(T), layout.count);
      values.assign(layout.count, fallback);
    }
  }
  void fill(T value, bool materialize = true) {
    fallback = value;
    if (materialize) allocate();
    if (allocated()) std::fill(values.begin(), values.end(), value);
  }
  struct Filter {
    Table& table; int s, f;
    T* operator[](int t) { return table.values.data() + table.layout.row(s, f, t); }
  };
  struct Stage {
    Table& table; int s;
    Filter operator[](int f) { return {table, s, f}; }
  };
  Stage operator[](int s) { return {*this, s}; }
  bool locate(std::size_t logical, std::size_t& physical) const {
    const auto a = logical % layout.dense_length;
    logical /= layout.dense_length;
    const int t = logical % 7, f = (logical / 7) % 4, s = logical / 28;
    std::size_t compact;
    if (s >= 2 || !layout.compact_index(s, f, a, compact)) return false;
    physical = layout.row(s, f, t) + compact;
    return true;
  }
  template<class Disk> void export_chunk(std::size_t start, Disk* out, std::size_t n) const {
    for (std::size_t i = 0; i < n; ++i) {
      std::size_t physical;
      out[i] = allocated() && locate(start + i, physical)
          ? static_cast<Disk>(values[physical]) : static_cast<Disk>(fallback);
    }
  }
  template<class Disk> void import_chunk(std::size_t start, const Disk* in, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      std::size_t physical;
      if (!locate(start + i, physical)) continue;
      const T value = static_cast<T>(in[i]);
      if (!allocated() && std::memcmp(&value, &fallback, sizeof(T)) == 0) continue;
      allocate();
      values[physical] = value;
    }
  }
};
#endif
}
#endif
