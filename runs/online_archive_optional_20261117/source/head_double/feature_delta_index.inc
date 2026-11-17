// Experimental same-stage candidate index derivation; no model reads here.
#ifndef FEATURE_SHARE_THRESHOLD
#define FEATURE_SHARE_THRESHOLD 384
#endif
struct FeatureDeltaContext {
  bool ready=false;
  int stage=0;
  alignas(16) int digits[16][4]{};
  alignas(16) uint32_t physical[UNROLL_COUNT*NUM_SPLIT]{};
};
static thread_local FeatureDeltaContext* active_feature_delta=nullptr;
struct FeatureDeltaTables {
  struct Member { uint8_t tuple,position; };
  std::array<std::array<Member,UNROLL_COUNT>,16> members{};
  std::array<int,16> counts{};
  alignas(16) int powers[2][TUPLE_SIZE][4]{};
  FeatureDeltaTables() {
    for(int i=0;i<UNROLL_COUNT;++i)for(int k=0;k<TUPLE_SIZE;++k){
      int cell=posSym[i][k];members[cell][counts[cell]++]={static_cast<uint8_t>(i),static_cast<uint8_t>(k)};
    }
    for(int s=0;s<2;++s)for(int f=0;f<4;++f){
      int power=1;
      for(int k=TUPLE_SIZE-1;k>=0;--k){powers[s][k][f]=power;power*=NT9_INDEX_BASE(s,f);}
    }
  }
};
__attribute__((target("sse4.1"))) static bool feature_delta_generate(
    FeatureDeltaContext& c,int stage,const int (&digits)[16][4],uint32_t* physical) {
  if(!c.ready||stage!=c.stage)return false;
  static const FeatureDeltaTables tables;
  alignas(16) int diff[16][4];int changed[16],changed_count=0,m=0;
  for(int cell=0;cell<16;++cell){
    bool any=false;
    for(int f=0;f<4;++f){diff[cell][f]=digits[cell][f]-c.digits[cell][f];any|=diff[cell][f]!=0;}
    if(any){changed[changed_count++]=cell;m+=tables.counts[cell];}
  }
  if(m>FEATURE_SHARE_THRESHOLD)return false;
  std::memcpy(physical,c.physical,sizeof(c.physical));
  for(int x=0;x<changed_count;++x){
    const int cell=changed[x];
    const __m128i d=_mm_load_si128(reinterpret_cast<const __m128i*>(diff[cell]));
    for(int z=0;z<tables.counts[cell];++z){
      const auto member=tables.members[cell][z];
      auto* dest=reinterpret_cast<__m128i*>(physical+member.tuple*4);
      const auto old=_mm_loadu_si128(dest);
      const auto power=_mm_load_si128(reinterpret_cast<const __m128i*>(tables.powers[stage][member.position]));
      _mm_storeu_si128(dest,_mm_add_epi32(old,_mm_mullo_epi32(d,power)));
    }
  }
  return true;
}
static void feature_delta_remember(FeatureDeltaContext& c,int stage,const int (&digits)[16][4],const uint32_t* physical) {
  if(c.ready)return;
  c.ready=true;c.stage=stage;
  std::memcpy(c.digits,digits,sizeof(c.digits));
  std::memcpy(c.physical,physical,sizeof(c.physical));
}
