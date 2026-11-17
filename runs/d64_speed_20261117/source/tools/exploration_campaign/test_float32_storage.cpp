// Reduced-table D storage and arithmetic oracle. Build once per precision.
#include "../../learn_double/tdplayer_VSE_exploration.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>
double ALPHA=.5;
int main(int argc,char** argv) {
  assert(argc>=2);static_assert(TUPLE_SIZE==3);
#ifdef NT9_FLOAT32_STORAGE
  static_assert(std::is_same_v<compact_value_t,float>);
#else
  static_assert(std::is_same_v<compact_value_t,double>);
#endif
  init_movetable();init_tuple();exploration::prepare_model_access();
  for(auto access:{exploration_concurrency::Mode::serialized,exploration_concurrency::Mode::async}) {
    exploration_concurrency::active_mode=access;
    compact_value_t w=.123456789,e=1e-12,a=1e-12,rw=w,re=e,ra=a;
    uint8_t count=0;
    for(double input_delta:{1e-15,-1e-15,.123456789,-.03456789,1e-8}) {
      const learning_scalar_t delta=static_cast<learning_scalar_t>(input_delta);
      ra=static_cast<compact_value_t>(ra+std::fabs(delta));
      re=static_cast<compact_value_t>(re+delta);
      rw=static_cast<compact_value_t>(rw+delta*learning_scalar_t(.5)*std::fabs(re)/ra);
      exploration_concurrency::update_tc(17,w,e,a,count,delta);
      assert(w==rw&&e==re&&a==ra&&count==1&&std::isfinite(w)&&a>=0);
    }
    board_t b={1,1,2,1,2,0,1,0,0,0,0,0,0,0,0,0};
    auto features=cache_before_features(b);
    auto ev=Evs.values,er=Errs.values,ae=Aerrs.values;
    learning_scalar_t d=learning_scalar_t(.00123456789)/(AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM*NUM_SPLIT);
    for(auto index:features.physical) {
      ae[index]=static_cast<compact_value_t>(ae[index]+std::fabs(d));
      er[index]=static_cast<compact_value_t>(er[index]+d);
      ev[index]=static_cast<compact_value_t>(ev[index]+d*learning_scalar_t(.5)*std::fabs(er[index])/ae[index]);
    }
    learning_update_cached(features,.00123456789);
    assert(Evs.values==ev&&Errs.values==er&&Aerrs.values==ae);
  }
  // Evaluation must accumulate at the selected precision, not just store it.
  board_t eval_board={1,1,2,1,2,0,1,0,0,0,0,0,0,0,0,0};
  for(size_t i=0;i<Evs.values.size();++i)Evs.values[i]=compact_value_t(.1+(i%13)*.00001);
  const auto ef=cache_before_features(eval_board);
  learning_scalar_t expected=ef.baseline;
  for(auto index:ef.physical)expected+=Evs.values[index];
  assert(cached_before_value(ef)==expected&&calcEvFiltered(eval_board)==expected);
  const auto saved_ev=Evs.values,saved_er=Errs.values,saved_ae=Aerrs.values;
  const auto saved_mask=Updatecounts.values;
  assert(save_exploration_compact_state(argv[1]));
  assert(std::filesystem::file_size(argv[1])==sizeof(exploration_compact_checkpoint::Header)+
      progress_oi::profile_path.size()+compact_layout.count*(3*sizeof(compact_value_t)+1));
  Evs.fill(7);Errs.fill(8);Aerrs.fill(9);
  assert(load_exploration_compact_state(argv[1]));
  assert(Evs.values==saved_ev&&Errs.values==saved_er&&Aerrs.values==saved_ae&&Updatecounts.values==saved_mask);
  if(argc>2)assert(!load_exploration_compact_state(argv[2]));
  const std::string invalid=std::string(argv[1])+".invalid";
  for(int kind=0;kind<3;++kind) {
    if(kind==0)Evs.values[0]=std::numeric_limits<compact_value_t>::infinity();
    if(kind==1)Errs.values[0]=std::numeric_limits<compact_value_t>::quiet_NaN();
    if(kind==2)Aerrs.values[0]=-1;
    assert(save_exploration_compact_state(invalid.c_str()));
    Evs.values=saved_ev;Errs.values=saved_er;Aerrs.values=saved_ae;
    assert(!load_exploration_compact_state(invalid.c_str()));
    assert(Evs.values==saved_ev&&Errs.values==saved_er&&Aerrs.values==saved_ae);
  }
  std::filesystem::remove(invalid);
  printf("float_storage_test,PASS,value_bytes,%zu,entries,%zu\n",sizeof(compact_value_t),compact_layout.count);
}
