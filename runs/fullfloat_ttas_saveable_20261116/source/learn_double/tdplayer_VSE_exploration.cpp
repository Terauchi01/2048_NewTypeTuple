#if defined(NT9_FLOAT32_STORAGE) && !defined(NT9_COMPACT_STORAGE)
#error "NT9_FLOAT32_STORAGE requires NT9_COMPACT_STORAGE"
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <smmintrin.h>
#define EXPLORATION_SSE41_AVAILABLE 1
#else
#define EXPLORATION_SSE41_AVAILABLE 0
#endif
#include <cstdio>
#include <cerrno>
#include <cstdint>
#include <type_traits>
#include <random>
#include <cstring>
#include <string>
#include <unistd.h>
using namespace std;
#include "game2048.h"
#include "symmetric.h"
#include "tdplayer_VSE_symmetric2.h"
#include "util.h"
#include "progress_oi.h"
#include "exploration_learning_rule.h"
#include "exploration_policy.h"
#include "exploration_concurrency.h"
#include "exploration_candidate_cache.h"
#include <memory>
#include "full_spawn_target.h"
// #include "../head/fixed_q10.hpp"
#include <random>
using namespace std;

#define NUM_STAGES 2
#define EV_INIT 320000
#define UC_CLIP 1
// #define ALPHA 0.5
extern double ALPHA;

// ステージ判定用閾値（この値以上の数字があるかでステージを決定）
#define STAGE_THRESHOLD 14

// デバッグフラグ (1にするとデバッグ情報を表示)
#define DEBUG_FILTERED_BOARDS 0

// calcEv デバッグ出力 (1 にするとログを出す)
#ifndef DEBUG_CALC_EV
#define DEBUG_CALC_EV 0
#endif

#ifndef TUPLE_FILE_TYPE
#define TUPLE_FILE_TYPE 9
#endif
#if TUPLE_FILE_TYPE != 8 && TUPLE_FILE_TYPE != 9
#error "Exploration study requires VSE with eight- or nine-cell tuples"
#endif

#define MAX_TILE_VALUE 17

#if TUPLE_FILE_TYPE == 6
#include "../head/selected_6_tuples_sym.h"
static const uint8_t filter_mapping[1][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 }
};
// #define DEFAULT_UNROLL_COUNT 72
#elif TUPLE_FILE_TYPE == 0
#include "../head/selected_6G_tuples_sym.h"
static const uint8_t filter_mapping[1][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 }
};
// #define DEFAULT_UNROLL_COUNT 64
#elif TUPLE_FILE_TYPE == 7
#include "../head/selected_7_tuples_sym.h"
static const uint8_t filter_mapping[2][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9 }
};
// #define DEFAULT_UNROLL_COUNT 64
#elif TUPLE_FILE_TYPE == 8
#include "../head/selected_8_tuples_sym.h"
static const uint8_t filter_mapping[3][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5 }
};
// #define DEFAULT_UNROLL_COUNT 40
#elif TUPLE_FILE_TYPE == 9
#include "../head/selected_9_tuples_sym.h"
static const uint8_t filter_mapping[4][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5 }
};
// #define DEFAULT_UNROLL_COUNT 56
#else
#error "Invalid TUPLE_FILE_TYPE specified"
static const uint8_t filter_mapping[4][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5 }
};
#endif

// --- _Pragma ラッパ ---
// 文字列化
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#if defined(__CUDACC__)
#define UNROLL_PRAGMA(N) _Pragma(STR(unroll N))                     // NVCC
#elif defined(__clang__)
#define UNROLL_PRAGMA(N) _Pragma(STR(clang loop unroll_count(N)))   // Clang / AppleClang
#elif defined(__GNUC__)
#define UNROLL_PRAGMA(N) _Pragma(STR(GCC unroll N))                 // GCC
#else
#define UNROLL_PRAGMA(N) /* fallback: 何もしない */
#endif

// apply mapping to produce NUM_SPLIT filtered boards
static inline void apply_filters_from_mapping(const board_t &board, board_t filtered_boards[NUM_SPLIT]) {
  UNROLL_PRAGMA(NUM_SPLIT*16)
    for (int fi = 0; fi < NUM_SPLIT*16; ++fi) {
      int f = fi/16;
      int i = fi%16; 
      int v = board[i];
      filtered_boards[f][i] = filter_mapping[f][v];
    }
}

#include "nt9_compact_layout.h"

#ifdef NT9_STORAGE_TEST_DIGITS
#undef TUPLE_SIZE
#define TUPLE_SIZE NT9_STORAGE_TEST_DIGITS
#undef ARRAY_LENGTH
#define ARRAY_LENGTH static_cast<int>(nt9_compact::power(7, TUPLE_SIZE))
#endif

#ifdef FULL_SPAWN_TEST_TABLE
#undef ARRAY_LENGTH
#define ARRAY_LENGTH 4096
#endif

#ifdef NT9_COMPACT_STORAGE
#include "nt9_compact_backend.inc"
#include "exploration_compact_checkpoint.inc"
#else
double Evs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
double Errs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
double Aerrs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int Updatecounts[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int pos[NUM_TUPLE][TUPLE_SIZE];

std::size_t evaluation_checkpoint_bytes() { return sizeof(Evs); }

void resetErrs() {
  constexpr size_t EvsCount = size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;
  fill_n(&Errs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Aerrs[0][0][0][0], EvsCount, 1e-12);
}

void output_ev(int seed,int suffix) {
  char filename[1024];
  FILE *fp;

  int r = system("mkdir -p dat"); if (r != 0) return;
  sprintf(filename, "dat/tuples%d-seed%d-VSE-count%03d.dat", TUPLE_FILE_TYPE, seed, suffix);
  fp = fopen(filename, "wb");
  if (!fp) { perror("fopen"); return; }

  // タプルごとに ARRAY_LENGTH 個を書き出す（Evs_save を使わない）
  // 注意: ARRAY_LENGTH が大きければ動的確保にする
  double *buf = (double*)malloc(sizeof(double) * ARRAY_LENGTH);
  if (!buf) { fclose(fp); return; }

  for (int s = 0; s < NUM_STAGES; s++) {
    for (int f = 0; f < NUM_SPLIT; f++) {
      for (int t = 0; t < NUM_TUPLE; t++) {
        // まずゼロで初期化（元コードの未設定エントリは 0 だった想定）
        memset(buf, 0, sizeof(double) * ARRAY_LENGTH);

        for (int a = 0; a < ARRAY_LENGTH; a++) {
	  if (Updatecounts[s][f][t][a] < UC_CLIP) {
	    buf[a] = 0;
	  } else {
	    buf[a] = Evs[s][f][t][a];
	  }
        }

        size_t written = fwrite(buf, sizeof(double), ARRAY_LENGTH, fp);
        if (written != (size_t)ARRAY_LENGTH) {
          perror("fwrite");
          free(buf);
          fclose(fp);
          return;
        }
      }
    }
  }

  free(buf);
  fclose(fp);
}

bool input_ev_for_learning(const char* filename) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    perror(filename);
    return false;
  }

  constexpr size_t EvsCount =
      size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;
  constexpr long long expectedBytes =
      static_cast<long long>(EvsCount) * sizeof(double);

  if (fseek(fp, 0, SEEK_END) != 0) {
    perror("fseek");
    fclose(fp);
    return false;
  }
  const long fileBytes = ftell(fp);
  if (fileBytes < 0 || static_cast<long long>(fileBytes) != expectedBytes) {
    fprintf(stderr,
            "invalid dat size: %s (expected %lld bytes, got %ld bytes)\n",
            filename, expectedBytes, fileBytes);
    fclose(fp);
    return false;
  }
  rewind(fp);

  const size_t readCount =
      fread(&Evs[0][0][0][0], sizeof(double), EvsCount, fp);
  if (readCount != EvsCount) {
    fprintf(stderr, "failed to read dat: %s (%zu/%zu values)\n",
            filename, readCount, EvsCount);
    fclose(fp);
    return false;
  }
  if (fclose(fp) != 0) {
    perror("fclose");
    return false;
  }

  // Errs/Aerrs/Updatecounts are not stored in the legacy dat format.
  // Treat loaded values as already learned while restarting optimizer history.
  fill_n(&Errs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Aerrs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Updatecounts[0][0][0][0], EvsCount, 1);
  printf("loaded learning values from %s (%zu doubles)\n",
         filename, EvsCount);
  return true;
}

namespace {
constexpr char LEARNING_STATE_MAGIC[8] = {'V', 'S', 'E', 'S', 'T', 'A', 'T', 'E'};
constexpr uint32_t LEARNING_STATE_VERSION = 1;

struct LearningStateHeader {
  char magic[8];
  uint32_t version;
  uint32_t tuple_file_type;
  uint64_t num_stages;
  uint64_t num_split;
  uint64_t num_tuple;
  uint64_t array_length;
  int64_t step_count;
  int64_t loop_count;
  int64_t last_log;
};

string state_filename(const char* dat_filename) {
  return string(dat_filename) + ".state";
}

bool write_block(FILE* fp, const void* data, size_t item_size,
                 size_t item_count, const char* name) {
  if (fwrite(data, item_size, item_count, fp) == item_count) return true;
  fprintf(stderr, "failed to write %s to learning state\n", name);
  return false;
}

bool read_block(FILE* fp, void* data, size_t item_size,
                size_t item_count, const char* name) {
  if (fread(data, item_size, item_count, fp) == item_count) return true;
  fprintf(stderr, "failed to read %s from learning state\n", name);
  return false;
}
}  // namespace

bool output_learning_state(const char* dat_filename, long long step_count,
                           int loop_count, int last_log) {
  const string filename = state_filename(dat_filename);
  const string temporary = filename + ".tmp";
  FILE* fp = fopen(temporary.c_str(), "wb");
  if (!fp) {
    perror(temporary.c_str());
    return false;
  }

  const LearningStateHeader header = {
      {'V', 'S', 'E', 'S', 'T', 'A', 'T', 'E'},
      LEARNING_STATE_VERSION,
      TUPLE_FILE_TYPE,
      NUM_STAGES,
      NUM_SPLIT,
      NUM_TUPLE,
      ARRAY_LENGTH,
      step_count,
      loop_count,
      last_log,
  };
  constexpr size_t count =
      size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;

  bool ok =
      write_block(fp, &header, sizeof(header), 1, "header") &&
      write_block(fp, &Errs[0][0][0][0], sizeof(double), count, "Errs") &&
      write_block(fp, &Aerrs[0][0][0][0], sizeof(double), count, "Aerrs") &&
      write_block(fp, &Updatecounts[0][0][0][0], sizeof(int), count,
                  "Updatecounts");
  if (fclose(fp) != 0) {
    perror("fclose");
    ok = false;
  }
  if (!ok) {
    remove(temporary.c_str());
    return false;
  }
  if (rename(temporary.c_str(), filename.c_str()) != 0) {
    perror("rename learning state");
    remove(temporary.c_str());
    return false;
  }

  printf("saved learning state to %s\n", filename.c_str());
  return true;
}

bool input_learning_state(const char* dat_filename, long long* step_count,
                          int* loop_count, int* last_log) {
  const string filename = state_filename(dat_filename);
  FILE* fp = fopen(filename.c_str(), "rb");
  if (!fp) {
    if (errno != ENOENT) {
      perror(filename.c_str());
      return false;
    }
    fprintf(stderr,
            "warning: %s was not found; optimizer history and counters "
            "start from zero\n",
            filename.c_str());
    return true;
  }

  constexpr size_t count =
      size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;
  constexpr long long expected_bytes =
      sizeof(LearningStateHeader) +
      static_cast<long long>(count) *
          (sizeof(double) + sizeof(double) + sizeof(int));
  if (fseek(fp, 0, SEEK_END) != 0) {
    perror("fseek");
    fclose(fp);
    return false;
  }
  const long file_bytes = ftell(fp);
  if (file_bytes < 0 ||
      static_cast<long long>(file_bytes) != expected_bytes) {
    fprintf(stderr,
            "invalid learning state size: %s "
            "(expected %lld bytes, got %ld bytes)\n",
            filename.c_str(), expected_bytes, file_bytes);
    fclose(fp);
    return false;
  }
  rewind(fp);

  LearningStateHeader header{};
  if (!read_block(fp, &header, sizeof(header), 1, "header")) {
    fclose(fp);
    return false;
  }
  if (memcmp(header.magic, LEARNING_STATE_MAGIC, sizeof(header.magic)) != 0 ||
      header.version != LEARNING_STATE_VERSION ||
      header.tuple_file_type != TUPLE_FILE_TYPE ||
      header.num_stages != NUM_STAGES ||
      header.num_split != NUM_SPLIT ||
      header.num_tuple != NUM_TUPLE ||
      header.array_length != ARRAY_LENGTH) {
    fprintf(stderr, "incompatible learning state: %s\n", filename.c_str());
    fclose(fp);
    return false;
  }

  const bool ok =
      read_block(fp, &Errs[0][0][0][0], sizeof(double), count, "Errs") &&
      read_block(fp, &Aerrs[0][0][0][0], sizeof(double), count, "Aerrs") &&
      read_block(fp, &Updatecounts[0][0][0][0], sizeof(int), count,
                 "Updatecounts");
  if (fclose(fp) != 0) {
    perror("fclose");
    return false;
  }
  if (!ok) return false;

  *step_count = header.step_count;
  *loop_count = static_cast<int>(header.loop_count);
  *last_log = static_cast<int>(header.last_log);
  printf("loaded learning state from %s\n", filename.c_str());
  return true;
}

namespace {
// Native-endian checkpoint. Explicit ABI markers prevent incompatible loads.
constexpr char FULL_STATE_MAGIC[8] = {'V','S','E','F','U','L','L','1'};
constexpr uint32_t FULL_STATE_VERSION = 1;
constexpr uint64_t FNV_OFFSET = UINT64_C(14695981039346656037);
constexpr uint64_t FNV_PRIME = UINT64_C(1099511628211);
constexpr size_t FULL_STATE_CHUNK = 1024 * 1024;
constexpr size_t FULL_STATE_COUNT =
    size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;

struct FullStateHeader {
  char magic[8];
  uint32_t version;
  uint32_t endian;
  uint32_t double_size;
  uint32_t int_size;
  uint64_t tuple_file_type;
  uint64_t num_stages;
  uint64_t num_split;
  uint64_t num_tuple;
  uint64_t array_length;
  uint64_t model_hash;
  uint64_t profile_enabled;
  uint64_t profile_bins;
  uint64_t profile_hash;
  uint64_t profile_path_bytes;
  uint64_t block_hashes[4];
};

uint64_t fnv_bytes(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash = (hash ^ bytes[i]) * FNV_PRIME;
  }
  return hash;
}

uint64_t current_profile_hash() {
  uint64_t hash = FNV_OFFSET;
  for (const auto& interval : progress_oi::profile) {
    hash = fnv_bytes(hash, &interval.begin, sizeof(interval.begin));
    hash = fnv_bytes(hash, &interval.end, sizeof(interval.end));
    hash = fnv_bytes(hash, &interval.value, sizeof(interval.value));
    hash = fnv_bytes(hash, &interval.sample_count, sizeof(interval.sample_count));
  }
  return hash;
}

uint64_t current_model_hash() {
  // Include the actual lookup mappings, stage split, and update-rule revision.
  const uint64_t configuration[] = {
      TUPLE_SIZE, VARIATION_TILE, UNROLL_COUNT, AVAIL_TUPLE,
      SYMMETRIC_TUPLE_NUM, STAGE_THRESHOLD, UC_CLIP, EV_INIT_VALUE, 1,
  };
  uint64_t hash = fnv_bytes(FNV_OFFSET, configuration, sizeof(configuration));
  const uint64_t exploration_revision = UINT64_C(0x4558504c4f523031);
  const uint64_t exploration_configuration[] = {
      static_cast<uint64_t>(exploration::config.mode),
      static_cast<uint64_t>(exploration::config.formula),
      static_cast<uint64_t>(exploration::config.teacher),
      static_cast<uint64_t>(exploration::config.warmup_steps),
      static_cast<uint64_t>(exploration::config.decay_end_steps),
  };
  hash = fnv_bytes(hash, &exploration_revision, sizeof(exploration_revision));
  hash = fnv_bytes(hash, exploration_configuration, sizeof(exploration_configuration));
  hash = fnv_bytes(hash, &exploration::config.c, sizeof(exploration::config.c));
  hash = fnv_bytes(hash, exploration::formula_specification,
                   std::strlen(exploration::formula_specification));
  const uint64_t guei_revision = UINT64_C(0x5443504f53543035);
  const double guei_rates[] = {learning_rule::Schedule::base_rate, learning_rule::Schedule::tc_rate};
  hash = fnv_bytes(hash, &guei_revision, sizeof(guei_revision));
  hash = fnv_bytes(hash, guei_rates, sizeof(guei_rates));
  const uint64_t learning_configuration[] = {
      UINT64_C(0x54445443),
      static_cast<uint64_t>(learning_rule::active_schedule.mode),
      static_cast<uint64_t>(learning_rule::active_schedule.switch_step),
  };
  hash = fnv_bytes(hash, learning_configuration, sizeof(learning_configuration));
  if (full_spawn::active_target == full_spawn::Target::full_spawn) {
    const uint64_t target_revision = UINT64_C(0x46554c4c535031);
    hash = fnv_bytes(hash, &target_revision, sizeof(target_revision));
  }
  hash = fnv_bytes(hash, posSym, sizeof(posSym));
  hash = fnv_bytes(hash, filter_mapping, sizeof(filter_mapping));
  return hash;
}

FullStateHeader current_full_header() {
  FullStateHeader header{};
  memcpy(header.magic, FULL_STATE_MAGIC, sizeof(header.magic));
  header.version = FULL_STATE_VERSION;
  header.endian = UINT32_C(0x01020304);
  header.double_size = sizeof(double);
  header.int_size = sizeof(int);
  header.tuple_file_type = TUPLE_FILE_TYPE;
  header.num_stages = NUM_STAGES;
  header.num_split = NUM_SPLIT;
  header.num_tuple = NUM_TUPLE;
  header.array_length = ARRAY_LENGTH;
  header.model_hash = current_model_hash();
  header.profile_enabled = progress_oi::enabled();
  header.profile_bins = progress_oi::profile.size();
  header.profile_hash = current_profile_hash();
  header.profile_path_bytes = progress_oi::profile_path.size();
  return header;
}

struct FullStateBlock {
  void* data;
  size_t bytes;
  const char* name;
};

vector<FullStateBlock> full_state_blocks() {
  return {
      {&Evs[0][0][0][0], FULL_STATE_COUNT * sizeof(double), "Evs"},
      {&Errs[0][0][0][0], FULL_STATE_COUNT * sizeof(double), "Errs"},
      {&Aerrs[0][0][0][0], FULL_STATE_COUNT * sizeof(double), "Aerrs"},
      {&Updatecounts[0][0][0][0], FULL_STATE_COUNT * sizeof(int), "Updatecounts"},
  };
}
}  // namespace

bool save_full_training_state(const char* filename) {
  FullStateHeader header = current_full_header();
  if (header.profile_path_bytes > FULL_STATE_CHUNK) {
    fprintf(stderr, "profile path too long for full training state\n");
    return false;
  }
  string temporary = string(filename) + ".tmp.XXXXXX";
  const int descriptor = mkstemp(temporary.data());
  if (descriptor < 0) {
    perror(temporary.c_str());
    return false;
  }
  FILE* fp = fdopen(descriptor, "w+b");
  if (!fp) {
    perror("fdopen full training state");
    close(descriptor);
    remove(temporary.c_str());
    return false;
  }

  bool ok = write_block(fp, &header, sizeof(header), 1, "full-state header") &&
      write_block(fp, progress_oi::profile_path.data(), 1,
                  progress_oi::profile_path.size(), "profile path");
  const auto blocks = full_state_blocks();
  for (size_t block = 0; ok && block < blocks.size(); ++block) {
    const auto* bytes = static_cast<const unsigned char*>(blocks[block].data);
    uint64_t hash = FNV_OFFSET;
    for (size_t offset = 0; ok && offset < blocks[block].bytes;) {
      const size_t count = min(FULL_STATE_CHUNK, blocks[block].bytes - offset);
      hash = fnv_bytes(hash, bytes + offset, count);
      ok = write_block(fp, bytes + offset, 1, count, blocks[block].name);
      offset += count;
    }
    header.block_hashes[block] = hash;
  }
  if (ok) {
    ok = fseeko(fp, 0, SEEK_SET) == 0 &&
        write_block(fp, &header, sizeof(header), 1, "final full-state header") &&
        fflush(fp) == 0 && fsync(fileno(fp)) == 0;
  }
  if (fclose(fp) != 0) ok = false;
  if (ok && rename(temporary.c_str(), filename) != 0) ok = false;
  if (!ok) {
    fprintf(stderr, "failed to save full training state: %s (%s)\n",
            filename, strerror(errno));
    remove(temporary.c_str());
    return false;
  }
  printf("saved full training state to %s\n", filename);
  return true;
}

bool load_full_training_state(const char* filename) {
  FILE* fp = fopen(filename, "rb");
  if (!fp) {
    perror(filename);
    return false;
  }
  FullStateHeader header{};
  const FullStateHeader current = current_full_header();
  bool ok = read_block(fp, &header, sizeof(header), 1, "full-state header");
  if (ok) {
    ok = memcmp(header.magic, current.magic, sizeof(header.magic)) == 0 &&
        header.version == current.version && header.endian == current.endian &&
        header.double_size == current.double_size && header.int_size == current.int_size &&
        header.tuple_file_type == current.tuple_file_type &&
        header.num_stages == current.num_stages && header.num_split == current.num_split &&
        header.num_tuple == current.num_tuple && header.array_length == current.array_length &&
        header.model_hash == current.model_hash &&
        header.profile_enabled == current.profile_enabled &&
        header.profile_bins == current.profile_bins && header.profile_hash == current.profile_hash &&
        header.profile_path_bytes <= FULL_STATE_CHUNK;
  }
  const auto blocks = full_state_blocks();
  uint64_t expected_bytes = sizeof(header) + header.profile_path_bytes;
  for (const auto& block : blocks) expected_bytes += block.bytes;
  if (ok) {
    ok = fseeko(fp, 0, SEEK_END) == 0;
    const off_t actual_bytes = ok ? ftello(fp) : -1;
    ok = actual_bytes >= 0 && static_cast<uint64_t>(actual_bytes) == expected_bytes;
  }
  if (!ok) {
    fprintf(stderr, "incompatible/truncated full training state (shape, model, profile or size): %s\n",
            filename);
    fclose(fp);
    return false;
  }

  // Preflight the complete payload before modifying any model array. This
  // catches truncated/corrupt files with only one small scratch buffer.
  const off_t payload_offset = sizeof(header) + header.profile_path_bytes;
  ok = fseeko(fp, payload_offset, SEEK_SET) == 0;
  vector<unsigned char> buffer(FULL_STATE_CHUNK);
  for (size_t block = 0; ok && block < blocks.size(); ++block) {
    uint64_t hash = FNV_OFFSET;
    for (size_t offset = 0; ok && offset < blocks[block].bytes;) {
      const size_t count = min(FULL_STATE_CHUNK, blocks[block].bytes - offset);
      ok = read_block(fp, buffer.data(), 1, count, blocks[block].name);
      if (ok) hash = fnv_bytes(hash, buffer.data(), count);
      offset += count;
    }
    if (ok && hash != header.block_hashes[block]) {
      fprintf(stderr, "checksum mismatch in full training state %s block %s\n",
              filename, blocks[block].name);
      ok = false;
    }
  }
  if (ok) ok = fseeko(fp, payload_offset, SEEK_SET) == 0;
  for (const auto& block : blocks) {
    if (!ok) break;
    ok = read_block(fp, block.data, 1, block.bytes, block.name);
  }
  if (fclose(fp) != 0) ok = false;
  if (!ok) {
    fprintf(stderr, "failed to load full training state: %s\n", filename);
    return false;
  }
  printf("loaded full training state from %s\n", filename);
  return true;
}

#endif

#ifndef NT9_INDEX_BASE
#define NT9_INDEX_BASE(stage, f) VARIATION_TILE
#endif

void exploration::prepare_model_access() {
  if (exploration_concurrency::active_mode != exploration_concurrency::Mode::serialized &&
      exploration_concurrency::active_mode != exploration_concurrency::Mode::async)
    throw std::invalid_argument("invalid model access mode");
#ifdef NT9_COMPACT_STORAGE
  // TC observation/update arrays must never be allocated while readers are
  // active, including a first resumed batch whose stored arrays were uniform.
  if (exploration_concurrency::is_async()) compact_ensure_tc();
#endif
}

static void require_prepared_model_access() {
#ifdef NT9_COMPACT_STORAGE
  if (exploration_concurrency::is_async() &&
      !compact_statistics_ready.load(std::memory_order_acquire))
    throw std::runtime_error("prepare_model_access required before async workers");
#endif
}

static std::size_t physical_feature_key(int stage, int filter, int tuple, int index) {
#ifdef NT9_COMPACT_STORAGE
  return compact_layout.row(stage, filter, tuple) + index;
#else
  return ((std::size_t(stage)*NUM_SPLIT+filter)*NUM_TUPLE+tuple)*ARRAY_LENGTH+index;
#endif
}

// ステージ判定関数：STAGE_THRESHOLD以上の値があるかでステージを決定
inline int get_stage(const board_t &board) {
  UNROLL_PRAGMA(16)
    for (int i = 0; i < 16; i++) {
      if (board[i] >= STAGE_THRESHOLD) {
	return 1; // 高ステージ
      }
    }
  return 0; // 低ステージ
}

#ifdef NT9_FLOAT32_STORAGE
using learning_scalar_t = float;
#else
using learning_scalar_t = double;
#endif
// フィルターで評価値を合計する関数
td_value_t calcEvFiltered(const board_t &board) {
  require_prepared_model_access();
  learning_scalar_t ev = static_cast<learning_scalar_t>(progress_oi::value(board));
  int stage = get_stage(board);
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用 (mapping 方式)
  apply_filters_from_mapping(board, filtered_boards);
  UNROLL_PRAGMA(UNROLL_COUNT)
    for (int i = 0; i < UNROLL_COUNT; i++) {
      UNROLL_PRAGMA(NUM_SPLIT)
	for (int f = 0; f < NUM_SPLIT; f++) {
	  int index = 0;
	  UNROLL_PRAGMA(TUPLE_SIZE)
	    for (int k = 0; k < TUPLE_SIZE; k++) {
	      index = index * NT9_INDEX_BASE(stage, f) + filtered_boards[f][posSym[i][k]];
	    }
	  int base = i % NUM_TUPLE;
	  #ifdef FULL_SPAWN_TEST_TABLE
          index %= ARRAY_LENGTH;
#endif
          learning_scalar_t val = exploration_concurrency::load(Evs[stage][f][base][index]);
	  ev += val;
	}
    }
  return ev;
}

void init_tuple() {
  exploration::config.validate();
  learning_rule::active_schedule.validate();
  // build_filter_mapping();
  printf("TDPlayer 2 Symmetric ver.\n");
  for (int i = 0; i < NUM_TUPLE; i++) {
    printf(" %2d-th-tuples: [%2d", (i+1),tuples[i][0]);
    for (int j = 1; j < TUPLE_SIZE; j++) printf(",%2d", tuples[i][j]);
    printf("]\n");
  }
  // 評価値の初期化: 全て0で初期化
  [[maybe_unused]] constexpr size_t EvsCount = size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;
  const learning_scalar_t initial_value =
      progress_oi::enabled() ? learning_scalar_t(0) : static_cast<learning_scalar_t>(EV_INIT_VALUE);
#ifdef NT9_COMPACT_STORAGE
  compact_initialize(initial_value);
#else
  fill_n(&Evs[0][0][0][0], EvsCount, initial_value);
  fill_n(&Errs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Aerrs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Updatecounts[0][0][0][0], EvsCount, 0);
#endif
}

bool load_progress_oi_profile(const char* filename) {
  return progress_oi::load(filename);
}

bool progress_oi_is_enabled() { return progress_oi::enabled(); }

td_value_t progress_oi_baseline(const board_t& board) {
  return progress_oi::value(board);
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

// Per-transition immutable feature cache. Values remain live and are read in
// original tuple/symmetry/filter order, including duplicate physical features.
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
struct CachedBeforeFeatures {
  learning_scalar_t baseline;
  uint32_t physical[UNROLL_COUNT * NUM_SPLIT];
};
static_assert(UNROLL_COUNT == AVAIL_TUPLE * SYMMETRIC_TUPLE_NUM);
// Compact offsets are bounded by the dense address space; retain uint32_t
// only while every valid physical offset is representable.
static_assert(uint64_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE *
                  nt9_compact::power(VARIATION_TILE, TUPLE_SIZE) <= UINT64_C(0x100000000),
              "NT9 index cache requires physical indices to fit uint32_t");
static CachedBeforeFeatures cache_before_features(const board_t& board) {
  CachedBeforeFeatures result;
  result.baseline = static_cast<learning_scalar_t>(progress_oi::value(board));
  const int stage = get_stage(board);
  board_t filtered_boards[NUM_SPLIT];
  apply_filters_from_mapping(board, filtered_boards);
  UNROLL_PRAGMA(UNROLL_COUNT)
  for (int i=0; i<UNROLL_COUNT; ++i) {
    UNROLL_PRAGMA(NUM_SPLIT)
    for (int f=0; f<NUM_SPLIT; ++f) {
      int index=0;
      UNROLL_PRAGMA(TUPLE_SIZE)
      for (int k=0; k<TUPLE_SIZE; ++k)
        index=index*NT9_INDEX_BASE(stage,f)+filtered_boards[f][posSym[i][k]];
      result.physical[i*NUM_SPLIT+f]=static_cast<uint32_t>(compact_layout.row(stage,f,i%NUM_TUPLE)+index);
    }
  }
  return result;
}
static learning_scalar_t cached_before_value(const CachedBeforeFeatures& features) {
  require_prepared_model_access();
  learning_scalar_t result=features.baseline;
  UNROLL_PRAGMA(UNROLL_COUNT)
  for (int i=0; i<UNROLL_COUNT; ++i) {
    UNROLL_PRAGMA(NUM_SPLIT)
    for (int f=0; f<NUM_SPLIT; ++f) {
#ifdef PROBE_READ_AHEAD
      const int j=i*NUM_SPLIT+f;
      if (j+PROBE_READ_AHEAD < UNROLL_COUNT*NUM_SPLIT)
        __builtin_prefetch(&Evs.values[features.physical[j+PROBE_READ_AHEAD]],0,1);
#endif
      result+=exploration_concurrency::load(Evs.values[features.physical[i*NUM_SPLIT+f]]);
    }
  }
  return result;
}
template<bool Async, bool TC>
static void update_cached_specialized(const CachedBeforeFeatures& features, learning_scalar_t stage_delta) {
  UNROLL_PRAGMA(AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM)
  for (int i=0; i<AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM; ++i) {
    UNROLL_PRAGMA(NUM_SPLIT)
    for(int f=0; f<NUM_SPLIT; ++f) {
      const int j=i*NUM_SPLIT+f;
#ifdef NT9_AOS_PROBE
      // W/E/A share the same AoS entry; one prefetch fetches all three.
      if(j+16<UNROLL_COUNT*NUM_SPLIT)
        __builtin_prefetch(&Evs.values[features.physical[j+16]],0,0);
#else
      if(j+8<UNROLL_COUNT*NUM_SPLIT) {
        const auto ahead=features.physical[j+8];
        __builtin_prefetch(&Evs.values[ahead],0,1);
        if constexpr(TC) {
          __builtin_prefetch(&Errs.values[ahead],0,1);
          __builtin_prefetch(&Aerrs.values[ahead],0,1);
        }
      }
#endif
      const uint32_t index=features.physical[j];
      auto& weight=Evs.values[index];
      auto& count=Updatecounts.values[index];
      if constexpr(Async) {
        std::lock_guard<exploration_concurrency::FeatureMutex> lock(exploration_concurrency::feature_mutex(index));
        auto local_weight=exploration_concurrency::atomic_load(weight);
        if constexpr(TC) {
          auto& err=Errs.values[index];auto& abs=Aerrs.values[index];
          auto e=exploration_concurrency::atomic_load(err), a=exploration_concurrency::atomic_load(abs);
          learning_rule::update(local_weight,e,a,stage_delta,learning_rule::Rule::tc);
          exploration_concurrency::atomic_store(abs,a);exploration_concurrency::atomic_store(err,e);
        } else {
          decltype(local_weight) e=0,a=0;learning_rule::update(local_weight,e,a,stage_delta,learning_rule::Rule::td);
        }
        exploration_concurrency::atomic_store(weight,local_weight);
        exploration_concurrency::atomic_store(count,uint8_t(1));
      } else if constexpr(TC) {
        learning_rule::update(weight,Errs.values[index],Aerrs.values[index],stage_delta,learning_rule::Rule::tc);count=1;
      } else {
        std::remove_reference_t<decltype(weight)> e=0,a=0;learning_rule::update(weight,e,a,stage_delta,learning_rule::Rule::td);count=1;
      }
    }
  }
}
static void learning_update_cached(const CachedBeforeFeatures& features,learning_scalar_t delta) {
  require_prepared_model_access();
  const learning_scalar_t stage_delta=static_cast<learning_scalar_t>(delta)/(AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM*NUM_SPLIT);
  const bool tc=learning_rule::batch_rule==learning_rule::Rule::tc;
  if(tc)compact_ensure_tc();
  if(exploration_concurrency::is_async()) {
    if(tc)update_cached_specialized<true,true>(features,stage_delta);
    else update_cached_specialized<true,false>(features,stage_delta);
  } else {
    if(tc)update_cached_specialized<false,true>(features,stage_delta);
    else update_cached_specialized<false,false>(features,stage_delta);
  }
}
#endif

// Bounded transient episode history: one allocation, no geometric growth.
// Only one TDPlayer episode may be active on each worker thread.
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
struct CachedCandidateTurn {
  CachedBeforeFeatures features[4];
  int rewards[4];
  bool legal[4];
  int chosen;
};
struct CandidateHistory {
  std::unique_ptr<CachedCandidateTurn[]> turns;
  std::size_t capacity=0, configured_budget=0;
  const TDPlayer* owner=nullptr;
  exploration_candidate_cache::Counters statistics;
};
static thread_local CandidateHistory candidate_history;
static void start_candidate_history(const TDPlayer* owner) {
  auto& h=candidate_history;
  h.owner=owner;
  if (!exploration_candidate_cache::enabled) return;
  const std::size_t budget=exploration_candidate_cache::budget_bytes;
  if (budget!=16u*1024u*1024u && budget!=32u*1024u*1024u)
    throw std::runtime_error("candidate cache budget must be 16 or 32 MiB");
  if (!h.turns || h.configured_budget!=budget) {
    h.turns.reset(); // Release before replacement: never exceed the hard budget.
    h.capacity=budget/sizeof(CachedCandidateTurn);
    h.turns=std::make_unique<CachedCandidateTurn[]>(h.capacity);
    h.configured_budget=budget;
    h.statistics.allocated_bytes=h.capacity*sizeof(CachedCandidateTurn);
    h.statistics.capacity_turns=h.capacity;
  }
}
static CachedCandidateTurn* candidate_turn(const TDPlayer* owner, int turn) {
  auto& h=candidate_history;
  if (!exploration_candidate_cache::enabled || turn<0) return nullptr;
  if (h.owner!=owner) throw std::runtime_error("interleaved TDPlayer episodes on one worker");
  return static_cast<std::size_t>(turn)<h.capacity ? &h.turns[turn] : nullptr;
}
static const CachedBeforeFeatures* selected_features(const TDPlayer* owner,int turn) {
  auto* cached=candidate_turn(owner,turn);
  return cached ? &cached->features[cached->chosen] : nullptr;
}
static learning_scalar_t cached_greedy_target(const CachedCandidateTurn& turn) {
  learning_scalar_t best=-std::numeric_limits<learning_scalar_t>::infinity();
  bool any=false;
  for(int action=0;action<4;++action) {
    if(!turn.legal[action])continue;
    const learning_scalar_t target=turn.rewards[action]+cached_before_value(turn.features[action]);
    if(!any || target>best)best=target;
    any=true;
  }
  return any ? best : learning_scalar_t(0);
}
#endif
exploration_candidate_cache::Counters exploration_candidate_cache::counters() {
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  return candidate_history.statistics;
#else
  return {};
#endif
}

// 学習のための関数２
[[maybe_unused]] static void learningUpdate(const board_t& before, learning_scalar_t delta)
{
  require_prepared_model_access();
  // 差分をタプル数,フィルター数で割る
  learning_scalar_t stage_delta = static_cast<learning_scalar_t>(delta);
  stage_delta = stage_delta / (AVAIL_TUPLE * SYMMETRIC_TUPLE_NUM * NUM_SPLIT);

  int stage = get_stage(before);
#ifdef NT9_COMPACT_STORAGE
  if (learning_rule::batch_rule == learning_rule::Rule::tc) compact_ensure_tc();
#endif
  
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用 (mapping 方式)
  apply_filters_from_mapping(before, filtered_boards);
  UNROLL_PRAGMA(AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM)
    for (int k = 0; k < AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM; k++) { // タプルごとのループ
      UNROLL_PRAGMA(NUM_SPLIT)
	for (int f = 0; f < NUM_SPLIT; f++) { // フィルターごとのループ
	  int index = 0;
	  UNROLL_PRAGMA(TUPLE_SIZE)
	    for (int j = 0; j < TUPLE_SIZE; j++) {
	      index = index * NT9_INDEX_BASE(stage, f) + filtered_boards[f][posSym[k][j]];
	    }
	  int base = k % NUM_TUPLE;
	  #ifdef FULL_SPAWN_TEST_TABLE
          index %= ARRAY_LENGTH;
#endif
          const std::size_t key = physical_feature_key(stage, f, base, index);
          if (learning_rule::batch_rule == learning_rule::Rule::td) {
            exploration_concurrency::update_td(key, Evs[stage][f][base][index],
                                               Updatecounts[stage][f][base][index], stage_delta);
          } else {
            exploration_concurrency::update_tc(key, Evs[stage][f][base][index],
                                               Errs[stage][f][base][index],
                                               Aerrs[stage][f][base][index],
                                               Updatecounts[stage][f][base][index], stage_delta);
          }
	}
    }
}

void learning(const board_t &before, const board_t &after, int rewards)
{
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  const CachedBeforeFeatures features=cache_before_features(before);
  const learning_scalar_t thisEvV=cached_before_value(features);
#else
  const learning_scalar_t thisEvV = calcEvFiltered(before);
#endif
  const learning_scalar_t nextEvV = calcEvFiltered(after);
  const learning_scalar_t delta = rewards + (nextEvV - thisEvV); // Preserve floating-point operation order.
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  learning_update_cached(features,delta);
#else
  learningUpdate(before, delta);
#endif
}  

 void learningLast(const board_t &before)
{
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  const CachedBeforeFeatures features=cache_before_features(before);
  const learning_scalar_t thisEvV=cached_before_value(features);
#else
  const learning_scalar_t thisEvV = calcEvFiltered(before);
#endif
  const learning_scalar_t delta = 0 + (0 - thisEvV); // Preserve terminal continuation value zero.
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  learning_update_cached(features,delta);
#else
  learningUpdate(before, delta);
#endif
}

void TDPlayer::gameStart()
{
  firstTurn = true;
  train_count = 0;
  archive_source_pending_ = false;
  archive_source_updates_ = 0;
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  start_candidate_history(this);
#else
  if(exploration_candidate_cache::enabled)
    throw std::runtime_error("candidate cache requires compact storage with index cache");
#endif
}

void TDPlayer::set_archive_source_afterstate(const board_t& board) {
  if (!firstTurn || train_count != 0 || archive_source_pending_ || archive_source_updates_ != 0)
    throw std::runtime_error("archive source must be registered once before the first action");
  if (exploration::config.teacher != exploration::Teacher::greedy_max)
    throw std::runtime_error("archive source update requires greedy-max teacher");
  std::copy(board, board + 16, archive_source_afterstate_);
  archive_source_pending_ = true;
}

// Evaluate A minima in exactly the same tuple/filter traversal as raw V.
// Both behavior modes pay this observation cost; no extra feature-count table.
static learning_scalar_t value_and_minimum_A(const board_t& board, learning_scalar_t& minimum_A
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
                                 , CachedBeforeFeatures* saved=nullptr
#endif
                                 ) {
  require_prepared_model_access();
  const int stage = get_stage(board);
  board_t filtered_boards[NUM_SPLIT];
  apply_filters_from_mapping(board, filtered_boards);
  learning_scalar_t value = static_cast<learning_scalar_t>(progress_oi::value(board));
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  if(saved)saved->baseline=value;
#endif
  learning_scalar_t minimum_accumulator = std::numeric_limits<learning_scalar_t>::infinity();
#ifdef NT9_COMPACT_STORAGE
  const bool history_allocated = Aerrs.allocated();
#endif
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  uint32_t physical[UNROLL_COUNT * NUM_SPLIT];
  for (int i=0; i<UNROLL_COUNT; ++i) {
    for (int f=0; f<NUM_SPLIT; ++f) {
      int index=0;
      for (int k=0; k<TUPLE_SIZE; ++k)
        index=index*NT9_INDEX_BASE(stage,f)+filtered_boards[f][posSym[i][k]];
#ifdef FULL_SPAWN_TEST_TABLE
      index %= ARRAY_LENGTH;
#endif
      physical[i*NUM_SPLIT+f]=static_cast<uint32_t>(compact_layout.row(stage,f,i%NUM_TUPLE)+index);
    }
  }
  for (int j=0; j<UNROLL_COUNT*NUM_SPLIT; ++j) {
    if(j+32<UNROLL_COUNT*NUM_SPLIT) {
      __builtin_prefetch(&Evs.values[physical[j+32]],0,1);
      if(history_allocated) __builtin_prefetch(&Aerrs.values[physical[j+32]],0,1);
    }
    const auto index=physical[j];
    if(saved) saved->physical[j]=index;
    value += exploration_concurrency::load(Evs.values[index]);
    const learning_scalar_t absolute_sum=history_allocated ? exploration_concurrency::load(Aerrs.values[index]) : learning_scalar_t(1e-12);
    minimum_accumulator=std::min(minimum_accumulator,absolute_sum);
  }
#else
  for (int i = 0; i < UNROLL_COUNT; ++i) {
    for (int f = 0; f < NUM_SPLIT; ++f) {
      int index = 0;
      for (int k = 0; k < TUPLE_SIZE; ++k)
        index = index * NT9_INDEX_BASE(stage, f) + filtered_boards[f][posSym[i][k]];
#ifdef FULL_SPAWN_TEST_TABLE
      index %= ARRAY_LENGTH;
#endif
      const int base = i % NUM_TUPLE;
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
      if(saved)saved->physical[i*NUM_SPLIT+f]=static_cast<uint32_t>(compact_layout.row(stage,f,base)+index);
#endif
      value += exploration_concurrency::load(Evs[stage][f][base][index]);
#ifdef NT9_COMPACT_STORAGE
      const learning_scalar_t absolute_sum = history_allocated
          ? exploration_concurrency::load(Aerrs[stage][f][base][index]) : learning_scalar_t(1e-12);
#else
      const learning_scalar_t absolute_sum = exploration_concurrency::load(Aerrs[stage][f][base][index]);
#endif
      minimum_accumulator = std::min(minimum_accumulator, absolute_sum);
    }
  }
#endif
#ifdef NT9_COMPACT_STORAGE
  minimum_A=history_allocated ? minimum_accumulator : learning_scalar_t(1e-12);
#else
  minimum_A=minimum_accumulator;
#endif
  return value;
}

#if EXPLORATION_SSE41_AVAILABLE && defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
#ifdef FEATURE_INDEX_DELTA
#include "feature_delta_index.inc"
#endif
__attribute__((target("sse4.1"))) static learning_scalar_t value_and_minimum_A_sse(const board_t& board, learning_scalar_t& minimum_A
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
                                 , CachedBeforeFeatures* saved=nullptr
#endif
                                 ) {
  require_prepared_model_access();
  const int stage = get_stage(board);
  board_t filtered_boards[NUM_SPLIT];
  apply_filters_from_mapping(board, filtered_boards);
  learning_scalar_t value = static_cast<learning_scalar_t>(progress_oi::value(board));
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  if(saved)saved->baseline=value;
#endif
  learning_scalar_t minimum_accumulator = std::numeric_limits<learning_scalar_t>::infinity();
#ifdef NT9_COMPACT_STORAGE
  const bool history_allocated = Aerrs.allocated();
#endif
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  static_assert(NUM_SPLIT == 4);
  uint32_t physical[UNROLL_COUNT * NUM_SPLIT];
  alignas(16) int digits_by_cell[16][4];
  for (int c=0;c<16;++c)
    for (int f=0;f<4;++f) digits_by_cell[c][f]=filtered_boards[f][c];
  const __m128i bases = _mm_setr_epi32(NT9_INDEX_BASE(stage,0), NT9_INDEX_BASE(stage,1), NT9_INDEX_BASE(stage,2), NT9_INDEX_BASE(stage,3));
#ifdef FEATURE_INDEX_DELTA
  const bool derived=active_feature_delta && feature_delta_generate(*active_feature_delta,stage,digits_by_cell,physical);
  if(!derived) {
#endif
  for (int i=0; i<UNROLL_COUNT; ++i) {
    __m128i index = _mm_setzero_si128();
    for (int k=0; k<TUPLE_SIZE; ++k) {
      const int cell=posSym[i][k];
      const __m128i digit=_mm_load_si128(reinterpret_cast<const __m128i*>(digits_by_cell[cell]));
      index=_mm_add_epi32(_mm_mullo_epi32(index,bases),digit);
    }
    alignas(16) uint32_t indices[4];_mm_store_si128(reinterpret_cast<__m128i*>(indices),index);
    for(int f=0;f<4;++f) {
#ifdef FULL_SPAWN_TEST_TABLE
      indices[f] %= ARRAY_LENGTH;
#endif
      physical[i*NUM_SPLIT+f]=static_cast<uint32_t>(compact_layout.row(stage,f,i%NUM_TUPLE)+indices[f]);
    }
  }
#ifdef FEATURE_INDEX_DELTA
  if(active_feature_delta)feature_delta_remember(*active_feature_delta,stage,digits_by_cell,physical);
  }
#endif
  for (int j=0; j<UNROLL_COUNT*NUM_SPLIT; ++j) {
    if(j+32<UNROLL_COUNT*NUM_SPLIT) {
      __builtin_prefetch(&Evs.values[physical[j+32]],0,1);
      if(history_allocated) __builtin_prefetch(&Aerrs.values[physical[j+32]],0,1);
    }
    const auto index=physical[j];
    if(saved) saved->physical[j]=index;
    value += exploration_concurrency::load(Evs.values[index]);
    const learning_scalar_t absolute_sum=history_allocated ? exploration_concurrency::load(Aerrs.values[index]) : learning_scalar_t(1e-12);
    minimum_accumulator=std::min(minimum_accumulator,absolute_sum);
  }
#else
  for (int i = 0; i < UNROLL_COUNT; ++i) {
    for (int f = 0; f < NUM_SPLIT; ++f) {
      int index = 0;
      for (int k = 0; k < TUPLE_SIZE; ++k)
        index = index * NT9_INDEX_BASE(stage, f) + filtered_boards[f][posSym[i][k]];
#ifdef FULL_SPAWN_TEST_TABLE
      index %= ARRAY_LENGTH;
#endif
      const int base = i % NUM_TUPLE;
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
      if(saved)saved->physical[i*NUM_SPLIT+f]=static_cast<uint32_t>(compact_layout.row(stage,f,base)+index);
#endif
      value += exploration_concurrency::load(Evs[stage][f][base][index]);
#ifdef NT9_COMPACT_STORAGE
      const learning_scalar_t absolute_sum = history_allocated
          ? exploration_concurrency::load(Aerrs[stage][f][base][index]) : learning_scalar_t(1e-12);
#else
      const learning_scalar_t absolute_sum = exploration_concurrency::load(Aerrs[stage][f][base][index]);
#endif
      minimum_accumulator = std::min(minimum_accumulator, absolute_sum);
    }
  }
#endif
#ifdef NT9_COMPACT_STORAGE
  minimum_A=history_allocated ? minimum_accumulator : learning_scalar_t(1e-12);
#else
  minimum_A=minimum_accumulator;
#endif
  return value;
}

#endif

exploration::scalar_t exploration::greedy_state_target(const board_t& state) {
  learning_scalar_t best = -std::numeric_limits<learning_scalar_t>::infinity();
  bool any = false;
  for (int action = 0; action < 4; ++action) {
    board_t after;
    const int reward = moveB(state, after, static_cast<move_dir>(action));
    if (reward < 0) continue;
    const learning_scalar_t target = static_cast<learning_scalar_t>(reward) + static_cast<learning_scalar_t>(calcEvFiltered(after));
    if (!any || target > best) best = target;
    any = true;
  }
  return any ? best : learning_scalar_t(0);
}

void exploration::learn_greedy_state_target(const board_t& before,
                                           const board_t& next_state) {
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  const CachedBeforeFeatures features = cache_before_features(before);
  const learning_scalar_t current = cached_before_value(features);
  const learning_scalar_t delta = static_cast<learning_scalar_t>(greedy_state_target(next_state)) - current;
  learning_update_cached(features, delta);
#else
  const learning_scalar_t current = calcEvFiltered(before);
  const learning_scalar_t delta = static_cast<learning_scalar_t>(greedy_state_target(next_state)) - current;
  learningUpdate(before, delta);
#endif
}

enum move_dir TDPlayer::selectHand(const board_t& board,
                                   const alldir_bool& canMoves,
                                   const alldir_board& nextBoards,
                                   const alldir_int& scores) {
  std::array<bool, 4> legal{};
  std::array<learning_scalar_t, 4> values{}, minimum_A{};
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  CachedCandidateTurn* saved=candidate_turn(this,train_count);
  if(exploration_candidate_cache::enabled) {
    if(saved)++candidate_history.statistics.stored_turns;
    else ++candidate_history.statistics.fallback_turns;
  }
#endif
  auto value_fn = &value_and_minimum_A;
#if EXPLORATION_SSE41_AVAILABLE && defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE) && defined(FEATURE_INDEX_DELTA)
  FeatureDeltaContext delta_context;
  struct DeltaGuard { ~DeltaGuard(){active_feature_delta=nullptr;} } delta_guard;
  active_feature_delta=&delta_context;
#endif
#if EXPLORATION_SSE41_AVAILABLE && defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  if (__builtin_cpu_supports("sse4.1")) value_fn = &value_and_minimum_A_sse;
#endif
  for (int action = 0; action < 4; ++action) {
    legal[action] = canMoves[action];
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
    if(saved){saved->legal[action]=canMoves[action];saved->rewards[action]=scores[action];}
#endif
    if (canMoves[action]) {
      values[action] = scores[action] + value_fn(nextBoards[action], minimum_A[action]
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
          ,saved ? &saved->features[action] : nullptr
#endif
          );
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
      if(saved)++candidate_history.statistics.stored_legal_candidates;
#endif
    }
  }
  auto decision = exploration::choose(legal, values, minimum_A);
  if (decision.chosen < 0)
    throw std::runtime_error("selectHand called without a legal move");
  decision.stage = get_stage(board);
  decision.progress = progress_oi::calculate_progress(board);
  exploration::worker_context.decision = decision;
  if (train_count >= 100000) throw std::runtime_error("exploration episode history overflow");
  const int selected = decision.chosen;
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  if(saved)saved->chosen=selected;
#endif
  for (int cell = 0; cell < 16; ++cell) {
    train_before[train_count][cell] = board[cell];
    train_after[train_count][cell] = nextBoards[selected][cell];
  }
  train_score[train_count++] = scores[selected];
  firstTurn = false;
  return static_cast<move_dir>(selected);
}

void TDPlayer::gameEnd() {
  if (train_count == 0 && !archive_source_pending_) return;
  if (full_spawn::active_target != full_spawn::Target::sampled)
    throw std::runtime_error("exploration core uses sampled next states only");
  if (archive_source_pending_ && exploration::config.teacher != exploration::Teacher::greedy_max)
    throw std::runtime_error("archive source update requires greedy-max teacher");
  if (train_count == 0) {
    // A sampled spawn can immediately end the game. Its source then has a
    // zero continuation target, even though no action was recorded.
    learningLast(archive_source_afterstate_);
    archive_source_pending_ = false;
    ++archive_source_updates_;
    return;
  }
  --train_count;
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
  if(const auto* features=selected_features(this,train_count)) {
    const learning_scalar_t current=cached_before_value(*features);
    learning_update_cached(*features,0+(0-current));
  } else
#endif
  learningLast(train_after[train_count]);
  for (; train_count > 0; --train_count) {
#if defined(NT9_COMPACT_STORAGE) && !defined(NT9_DISABLE_INDEX_CACHE)
    const auto* current_features=selected_features(this,train_count-1);
    const auto* next_turn=candidate_turn(this,train_count);
    if(current_features) {
      const learning_scalar_t current=cached_before_value(*current_features);
      learning_scalar_t delta;
      if(exploration::config.teacher==exploration::Teacher::legacy) {
        const auto* next_features=selected_features(this,train_count);
        const learning_scalar_t next=next_features ? cached_before_value(*next_features) : calcEvFiltered(train_after[train_count]);
        delta=train_score[train_count]+(next-current);
      } else {
        const learning_scalar_t next=next_turn ? cached_greedy_target(*next_turn) : exploration::greedy_state_target(train_before[train_count]);
        delta=next-current;
      }
      learning_update_cached(*current_features,delta);
      continue;
    }
#endif
    if (exploration::config.teacher == exploration::Teacher::legacy) {
      learning(train_after[train_count - 1], train_after[train_count], train_score[train_count]);
    } else {
      exploration::learn_greedy_state_target(train_after[train_count - 1],
                                             train_before[train_count]);
    }
  }
  if (archive_source_pending_) {
    // Recompute the first state's greedy target after the continuation's
    // backward updates. Reward belongs to that next action, not the saved
    // source's already accumulated score. Restart segments never repeat this.
    exploration::learn_greedy_state_target(archive_source_afterstate_, train_before[0]);
    archive_source_pending_ = false;
    ++archive_source_updates_;
  }
}

void probe_fingerprint() {
  uint64_t h[4]={FNV_OFFSET,FNV_OFFSET,FNV_OFFSET,FNV_OFFSET};
  for(size_t i=0;i<compact_layout.count;++i) {
    h[0]=fnv_bytes(h[0],&Evs.values[i],sizeof(compact_value_t));
    h[1]=fnv_bytes(h[1],&Errs.values[i],sizeof(compact_value_t));
    h[2]=fnv_bytes(h[2],&Aerrs.values[i],sizeof(compact_value_t));
    h[3]=fnv_bytes(h[3],&Updatecounts.values[i],sizeof(uint8_t));
  }
  for(size_t i=0;i<4;++i)printf("probe_model_fnv,%zu,%llu\n",i,(unsigned long long)h[i]);
}
