#ifndef EXPLORATION_CONCURRENCY_H
#define EXPLORATION_CONCURRENCY_H

#include "exploration_learning_rule.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <pthread.h>
#include <cerrno>
#if defined(NT9_TTAS_MUTEX) && defined(NT9_FUTEX_MUTEX)
#error "TTAS and futex mutex implementations cannot be mixed"
#endif
#ifdef NT9_TTAS_MUTEX
#if !defined(__linux__) || !defined(__x86_64__)
#error "TTAS research mutex requires Linux x86-64"
#endif
#ifndef NT9_TTAS_YIELD_SPINS
#error "TTAS yield interval must be defined across all translation units"
#endif
#if NT9_TTAS_YIELD_SPINS < 1
#error "TTAS yield interval must be positive"
#endif
#define NT9_TTAS_STRINGIFY_INNER(x) #x
#define NT9_TTAS_STRINGIFY(x) NT9_TTAS_STRINGIFY_INNER(x)
#include <thread>
#include <immintrin.h>
#endif
#if defined(NT9_FUTEX_MUTEX)
#ifndef NT9_FUTEX_SPIN
#error "NT9_FUTEX_SPIN must be specified in every translation unit"
#endif
#if NT9_FUTEX_SPIN != 0 && NT9_FUTEX_SPIN != 8 && NT9_FUTEX_SPIN != 32 && NT9_FUTEX_SPIN != 128
#error "unsupported spin count"
#endif
#define NT9_SPIN_STRINGIFY_INNER(x) #x
#define NT9_SPIN_STRINGIFY(x) NT9_SPIN_STRINGIFY_INNER(x)
#if !defined(__linux__) || !defined(__x86_64__)
#error "NT9_FUTEX_MUTEX is Linux x86-64 only"
#endif
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <climits>
#include <immintrin.h>
#endif

namespace exploration_concurrency {
enum class Mode { serialized, async };
// Set only while every worker is stopped. Serialized callers retain their
// external model mutex; async callers may evaluate/update concurrently.
inline Mode active_mode = Mode::serialized;
inline constexpr const char* concurrency_specification =
#ifdef NT9_FUTEX_MUTEX
    "futex012_boundedspin_" NT9_SPIN_STRINGIFY(NT9_FUTEX_SPIN) "_v1;"
#endif
#ifdef NT9_TTAS_MUTEX
    "ttas_spin_yield_" NT9_TTAS_STRINGIFY(NT9_TTAS_YIELD_SPINS) "_not_futex_research_v1;"
#endif
    "relaxed_atomic_ref_reads_stores;one_feature_stripe_lock_per_update;"
    "16384_cacheline_aligned_stripes;non_snapshot_values_and_targets;"
    "allocate_reset_save_load_only_when_workers_quiescent_v1";
inline constexpr std::size_t stripe_count = 16384;
#if defined(NT9_TTAS_MUTEX)
// Standalone research lock, with no futex waiters or wake-up obligations.
// Not fair: a descheduled owner or a stream of barging threads may delay
// waiters indefinitely. Never mix this lock identity with futex state.
static_assert(std::atomic<uint32_t>::is_always_lock_free);
#ifdef NT9_TTAS_TEST_COUNTERS
inline thread_local unsigned ttas_test_yields=0;
#endif
class FeatureMutex {
  std::atomic<uint32_t> state_{0};
public:
  FeatureMutex()=default;
  FeatureMutex(const FeatureMutex&)=delete;
  FeatureMutex& operator=(const FeatureMutex&)=delete;
  void lock() noexcept {
    uint32_t expected=0;
    if(state_.compare_exchange_strong(expected,1,std::memory_order_acquire,std::memory_order_relaxed))return;
    unsigned attempts=0;
    for(;;) {
      while(state_.load(std::memory_order_relaxed)!=0) {
        _mm_pause();
        if(++attempts>=NT9_TTAS_YIELD_SPINS){
#ifdef NT9_TTAS_TEST_COUNTERS
          ++ttas_test_yields;
#endif
          std::this_thread::yield();attempts=0;}
      }
      expected=0;
      if(state_.compare_exchange_weak(expected,1,std::memory_order_acquire,std::memory_order_relaxed))return;
      _mm_pause();
      if(++attempts>=NT9_TTAS_YIELD_SPINS){
#ifdef NT9_TTAS_TEST_COUNTERS
          ++ttas_test_yields;
#endif
          std::this_thread::yield();attempts=0;}
    }
  }
  void unlock() noexcept {state_.store(0,std::memory_order_release);}
};
inline unsigned adaptive_fallback_count(){return 0;}
#elif defined(NT9_FUTEX_MUTEX)
// Linux 0=unlocked, 1=locked/no known waiter, 2=locked/possibly waiting.
// Always publish state=2 before FUTEX_WAIT; kernel checks value atomically.
// A contended unlock publishes 0 and wakes one; EAGAIN/EINTR only retry.
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(sizeof(std::atomic<uint32_t>)==sizeof(uint32_t));
static_assert(alignof(std::atomic<uint32_t>)>=alignof(uint32_t));
class FeatureMutex {
  alignas(4) std::atomic<uint32_t> state_{0};
  uint32_t* address() noexcept {return reinterpret_cast<uint32_t*>(&state_);}
public:
  FeatureMutex()=default;
  FeatureMutex(const FeatureMutex&)=delete;
  FeatureMutex& operator=(const FeatureMutex&)=delete;
  void lock() {
    uint32_t expected=0;
    if(state_.compare_exchange_strong(expected,1,std::memory_order_acquire,std::memory_order_relaxed)) return;
    for(int i=0;i<NT9_FUTEX_SPIN;++i) {
      _mm_pause();expected=0;
      if(state_.compare_exchange_weak(expected,1,std::memory_order_acquire,std::memory_order_relaxed)) return;
    }
    while(state_.exchange(2,std::memory_order_acquire)!=0) {
      long rc=syscall(SYS_futex,address(),FUTEX_WAIT_PRIVATE,2,nullptr,nullptr,0);
      if(rc==-1 && errno!=EAGAIN && errno!=EINTR)throw std::system_error(errno,std::generic_category(),"futex wait");
    }
  }
  void unlock() noexcept {
    if(state_.fetch_sub(1,std::memory_order_release)!=1) {
      state_.store(0,std::memory_order_release);
      if(syscall(SYS_futex,address(),FUTEX_WAKE_PRIVATE,1,nullptr,nullptr,0)<0)std::terminate();
    }
  }
};
inline unsigned adaptive_fallback_count(){return 0;}
#elif defined(NT9_ADAPTIVE_MUTEX)
inline std::atomic<unsigned> adaptive_fallbacks{0};
class FeatureMutex {
  pthread_mutex_t mutex_{};
public:
  FeatureMutex() {
    pthread_mutexattr_t attr;
    int rc = pthread_mutexattr_init(&attr);
    if (rc) {
      adaptive_fallbacks.fetch_add(1, std::memory_order_relaxed);
      rc=pthread_mutex_init(&mutex_, nullptr);
      if(rc)throw std::system_error(rc,std::generic_category(),"pthread_mutex_init default fallback");
      return;
    }
#if defined(__linux__) && defined(__GLIBC__)
    const int adaptive_rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
#else
    const int adaptive_rc = ENOTSUP;
#endif
    if (adaptive_rc) adaptive_fallbacks.fetch_add(1, std::memory_order_relaxed);
    rc = pthread_mutex_init(&mutex_, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc && !adaptive_rc) {
      adaptive_fallbacks.fetch_add(1, std::memory_order_relaxed);
      rc = pthread_mutex_init(&mutex_, nullptr);
    }
    if (rc) throw std::system_error(rc, std::generic_category(), "pthread_mutex_init");
  }
  ~FeatureMutex() { pthread_mutex_destroy(&mutex_); }
  FeatureMutex(const FeatureMutex&) = delete;
  FeatureMutex& operator=(const FeatureMutex&) = delete;
  void lock() { const int rc=pthread_mutex_lock(&mutex_);if(rc)throw std::system_error(rc,std::generic_category(),"pthread_mutex_lock"); }
  void unlock() noexcept { if(pthread_mutex_unlock(&mutex_)!=0)std::terminate(); }
};
inline unsigned adaptive_fallback_count() { return adaptive_fallbacks.load(std::memory_order_relaxed); }
#else
using FeatureMutex = std::mutex;
inline unsigned adaptive_fallback_count() { return 0; }
#endif
struct alignas(64) Stripe { FeatureMutex mutex; };
inline std::array<Stripe, stripe_count> stripes;
static_assert(std::atomic_ref<double>::is_always_lock_free);
static_assert(alignof(double) >= std::atomic_ref<double>::required_alignment);

inline bool is_async() { return active_mode == Mode::async; }
inline Mode parse_mode(const std::string& text) {
  if (text == "serialized") return Mode::serialized;
  if (text == "async") return Mode::async;
  throw std::invalid_argument("model access must be serialized or async");
}
inline const char* mode_name(Mode mode) {
  if (mode == Mode::serialized) return "serialized";
  if (mode == Mode::async) return "async";
  throw std::invalid_argument("invalid model access mode");
}
inline FeatureMutex& feature_mutex(std::size_t key) {
  const uint64_t mixed = uint64_t(key) * UINT64_C(11400714819323198485);
  return stripes[(mixed >> 50) & (stripe_count-1)].mutex;
}
template<class T> inline T atomic_load(T& value) {
  static_assert(std::atomic_ref<T>::is_always_lock_free);
  static_assert(alignof(T) >= std::atomic_ref<T>::required_alignment);
  return std::atomic_ref<T>(value).load(std::memory_order_relaxed);
}
template<class T> inline T load(T& value) {
  return is_async() ? atomic_load(value) : value;
}
template<class T> inline void atomic_store(T& destination, T value) {
  static_assert(std::atomic_ref<T>::is_always_lock_free);
  static_assert(alignof(T) >= std::atomic_ref<T>::required_alignment);
  std::atomic_ref<T>(destination).store(value, std::memory_order_relaxed);
}

template<class T, class Count>
inline void update_td(std::size_t key, T& weight, Count& count, std::type_identity_t<T> delta) {
  if (!is_async()) {
    T unused_error = 0, unused_absolute = 0;
    learning_rule::update(weight, unused_error, unused_absolute, delta, learning_rule::Rule::td);
    count = 1;
    return;
  }
  std::lock_guard<FeatureMutex> lock(feature_mutex(key));
  T local_weight = atomic_load(weight), unused_error = 0, unused_absolute = 0;
  learning_rule::update(local_weight, unused_error, unused_absolute, delta, learning_rule::Rule::td);
  atomic_store(weight, local_weight);
  atomic_store(count, Count(1));
}

template<class T, class Count>
inline void update_tc(std::size_t key, T& weight, T& error,
                      T& absolute, Count& count, std::type_identity_t<T> delta) {
  if (!is_async()) {
    learning_rule::update(weight, error, absolute, delta, learning_rule::Rule::tc);
    count = 1;
    return;
  }
  // Every writer of this physical feature shares a stripe. Readers use only
  // atomic_ref too: a lock-protected ordinary double write would still race
  // with the lock-free atomic evaluation readers.
  std::lock_guard<FeatureMutex> lock(feature_mutex(key));
  T local_weight = atomic_load(weight);
  T local_error = atomic_load(error);
  T local_absolute = atomic_load(absolute);
  learning_rule::update(local_weight, local_error, local_absolute, delta, learning_rule::Rule::tc);
  atomic_store(absolute, local_absolute);
  atomic_store(error, local_error);
  atomic_store(weight, local_weight);
  atomic_store(count, Count(1));
}
}  // namespace exploration_concurrency

namespace exploration {
// Call after initialization/restoration/reset and before starting a worker
// wave. No allocation, reset or checkpoint IO may overlap active workers.
void prepare_model_access();
inline void prepareAsyncModel() { prepare_model_access(); }
}  // namespace exploration
#endif
