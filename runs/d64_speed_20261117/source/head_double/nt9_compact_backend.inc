// Opt-in NT9 backend. Existing builds and running binaries retain dense storage.
#if TUPLE_FILE_TYPE != 9 || defined(FULL_SPAWN_TEST_TABLE)
#error "Compact storage requires NT9 without modulo-reduced test tables"
#endif
#include "nt9_compact_storage.h"
#include <atomic>
#include <mutex>

#define NT9_INDEX_BASE(stage, f) nt9_compact::bases[stage][f]
#ifdef NT9_FLOAT32_STORAGE
using compact_value_t = float;
#else
using compact_value_t = double;
#endif
namespace {
static_assert(NUM_STAGES == 2 && NUM_SPLIT == 4 && NUM_TUPLE == 7 && STAGE_THRESHOLD == 14);
const nt9_compact::Layout compact_layout(TUPLE_SIZE);
#ifdef GUEI_LEARNING_RULE_SCHEDULE_H
constexpr compact_value_t compact_history_initial = compact_value_t(0);
#else
constexpr compact_value_t compact_history_initial = compact_value_t(1e-12);
#endif
std::mutex compact_statistics_mutex;
std::atomic<bool> compact_statistics_ready{false};
}
#ifdef NT9_AOS_PROBE
static_assert(std::is_same_v<compact_value_t,nt9_compact::aos_value_t>, "AoS and arithmetic precision must match");
nt9_compact::Table<compact_value_t,0> Evs(compact_layout);
nt9_compact::Table<compact_value_t,1> Errs(compact_layout);
nt9_compact::Table<compact_value_t,2> Aerrs(compact_layout);
nt9_compact::Table<uint8_t,3> Updatecounts(compact_layout);
#else
nt9_compact::Table<compact_value_t> Evs(compact_layout), Errs(compact_layout), Aerrs(compact_layout);
nt9_compact::Table<uint8_t> Updatecounts(compact_layout);
#endif
int pos[NUM_TUPLE][TUPLE_SIZE];

void compact_ensure_tc() {
  if (compact_statistics_ready.load(std::memory_order_acquire)) return;
  std::lock_guard<std::mutex> lock(compact_statistics_mutex);
  if (compact_statistics_ready.load(std::memory_order_relaxed)) return;
  Errs.allocate();
  Aerrs.allocate();
  compact_statistics_ready.store(true, std::memory_order_release);
  printf("compact_tc_allocated,entries,%zu,bytes,%zu\n", compact_layout.count,
         compact_layout.count * (2*sizeof(compact_value_t)));
}
void resetErrs() {
  std::lock_guard<std::mutex> lock(compact_statistics_mutex);
  Errs.fill(compact_history_initial, false);
  Aerrs.fill(compact_history_initial, false);
}
void compact_initialize(compact_value_t initial_value) {
  for (int s = 0; s < 2; ++s) for (int f = 0; f < 4; ++f) {
    int maximum = 0;
    for (int v = 0; v < (s == 0 ? STAGE_THRESHOLD : 18); ++v)
      maximum = std::max(maximum, int(filter_mapping[f][v]));
    if (maximum + 1 != nt9_compact::bases[s][f])
      throw std::runtime_error("compact radix no longer matches filter mapping");
  }
  Evs.fill(initial_value);
  Updatecounts.fill(0);
  resetErrs();
  if (learning_rule::active_schedule.mode == learning_rule::Mode::tc) compact_ensure_tc();
  printf("compact_storage,entries,%zu,weight_mask_bytes,%zu,tc_bytes,%zu\n",
         compact_layout.count, compact_layout.count * (sizeof(compact_value_t)+1),
         compact_statistics_ready.load() ? compact_layout.count * (2*sizeof(compact_value_t)) : 0);
}
std::size_t evaluation_checkpoint_bytes() {
  return size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH * sizeof(double);
}
// Disk formats remain dense V1 for existing evaluation/resume tools. Stream in
// bounded chunks instead of constructing a dense temporary model in memory.
constexpr size_t COMPACT_IO_ITEMS = 32768;
template<class Disk> bool compact_write(FILE* fp, int id, size_t start, size_t count,
                                       bool clipped = false) {
  std::vector<Disk> buffer(COMPACT_IO_ITEMS);
  std::vector<int> flags(clipped ? COMPACT_IO_ITEMS : 0);
  for (size_t done = 0; done < count;) {
    const size_t n = std::min(COMPACT_IO_ITEMS, count - done);
    switch (id) {
      case 0: Evs.export_chunk(start + done, buffer.data(), n); break;
      case 1: Errs.export_chunk(start + done, buffer.data(), n); break;
      case 2: Aerrs.export_chunk(start + done, buffer.data(), n); break;
      case 3: Updatecounts.export_chunk(start + done, buffer.data(), n); break;
    }
    if (clipped) {
      Updatecounts.export_chunk(start + done, flags.data(), n);
      for (size_t i = 0; i < n; ++i) if (flags[i] < UC_CLIP) buffer[i] = 0;
    }
    if (fwrite(buffer.data(), sizeof(Disk), n, fp) != n) return false;
    done += n;
  }
  return true;
}
template<class Disk> bool compact_read(FILE* fp, int id, size_t count) {
  std::vector<Disk> buffer(COMPACT_IO_ITEMS);
  for (size_t done = 0; done < count;) {
    const size_t n = std::min(COMPACT_IO_ITEMS, count - done);
    if (fread(buffer.data(), sizeof(Disk), n, fp) != n) return false;
    if (id == 3) for (size_t i = 0; i < n; ++i)
      if (buffer[i] != 0 && buffer[i] != 1) return false;
    switch (id) {
      case 0: Evs.import_chunk(done, buffer.data(), n); break;
      case 1: Errs.import_chunk(done, buffer.data(), n); break;
      case 2: Aerrs.import_chunk(done, buffer.data(), n); break;
      case 3: Updatecounts.import_chunk(done, buffer.data(), n); break;
    }
    done += n;
  }
  return true;
}
void output_ev(int seed, int suffix) {
#ifdef NT9_AOS_PROBE
  throw std::runtime_error("AoS benchmark: model IO disabled");
#endif
  char filename[1024];
  if (system("mkdir -p dat") != 0) return;
  snprintf(filename, sizeof(filename), "dat/tuples%d-seed%d-VSE-count%03d.dat",
           TUPLE_FILE_TYPE, seed, suffix);
  FILE* fp = fopen(filename, "wb");
  if (!fp) { perror(filename); return; }
  const bool ok = compact_write<double>(fp, 0, 0, evaluation_checkpoint_bytes()/8, true);
  if (fclose(fp) != 0 || !ok) throw std::runtime_error("compact dat export failed");
}

bool input_ev_for_learning(const char* filename) {
#ifdef NT9_AOS_PROBE
  throw std::runtime_error("AoS benchmark: model IO disabled");
#endif
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

  compact_initialize(progress_oi::enabled() ? 0.0 : static_cast<double>(EV_INIT_VALUE));
  const size_t readCount = compact_read<double>(fp, 0, EvsCount) ? EvsCount : 0;
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
  Errs.fill(compact_history_initial, false);
  Aerrs.fill(compact_history_initial, false);
  Updatecounts.fill(1);
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
#ifdef NT9_AOS_PROBE
  throw std::runtime_error("AoS benchmark: model IO disabled");
#endif
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
      compact_write<double>(fp, 1, 0, count) &&
      compact_write<double>(fp, 2, 0, count) &&
      compact_write<int>(fp, 3, 0, count);
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
#ifdef NT9_AOS_PROBE
  throw std::runtime_error("AoS benchmark: model IO disabled");
#endif
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
      compact_read<double>(fp, 1, count) &&
      compact_read<double>(fp, 2, count) &&
      compact_read<int>(fp, 3, count);
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
#ifdef NT9_FLOAT32_STORAGE
  const char storage_revision[]="float32_weights_tc_evaluation_td_ucb_arithmetic_v3";
  hash=fnv_bytes(hash,storage_revision,sizeof(storage_revision));
#endif
#ifdef GUEI_LEARNING_RULE_SCHEDULE_H
  const uint64_t guei_revision = UINT64_C(0x4755454950524531);
  const double guei_rates[] = {learning_rule::Schedule::base_rate, learning_rule::Schedule::tc_rate};
  hash = fnv_bytes(hash, &guei_revision, sizeof(guei_revision));
  hash = fnv_bytes(hash, guei_rates, sizeof(guei_rates));
#endif
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
#ifdef ACTION_SEARCH_H
  if (action_search::active_policy == action_search::Policy::expectimax) {
    const uint64_t action_revision = UINT64_C(0x4143545345415231);
    hash = fnv_bytes(hash, &action_revision, sizeof(action_revision));
  }
#endif
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
  int id;
  size_t bytes;
  const char* name;
};

vector<FullStateBlock> full_state_blocks() {
  return {
      {0, FULL_STATE_COUNT * sizeof(double), "Evs"},
      {1, FULL_STATE_COUNT * sizeof(double), "Errs"},
      {2, FULL_STATE_COUNT * sizeof(double), "Aerrs"},
      {3, FULL_STATE_COUNT * sizeof(int), "Updatecounts"},
  };
}
}  // namespace

bool save_full_training_state(const char* filename) {
#ifdef NT9_AOS_PROBE
  throw std::runtime_error("AoS benchmark: model IO disabled");
#endif
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
    const size_t item_size = block == 3 ? sizeof(int) : sizeof(double);
    vector<double> scratch(FULL_STATE_CHUNK / sizeof(double));
    vector<int> flag_scratch(block == 3 ? FULL_STATE_CHUNK / sizeof(int) : 0);
    auto* bytes = block == 3 ? reinterpret_cast<unsigned char*>(flag_scratch.data())
                             : reinterpret_cast<unsigned char*>(scratch.data());
    uint64_t hash = FNV_OFFSET;
    for (size_t offset = 0; ok && offset < blocks[block].bytes;) {
      const size_t count = min(FULL_STATE_CHUNK, blocks[block].bytes - offset);
      const size_t start = offset / item_size, n = count / item_size;
      switch (block) {
        case 0: Evs.export_chunk(start, reinterpret_cast<double*>(bytes), n); break;
        case 1: Errs.export_chunk(start, reinterpret_cast<double*>(bytes), n); break;
        case 2: Aerrs.export_chunk(start, reinterpret_cast<double*>(bytes), n); break;
        case 3: Updatecounts.export_chunk(start, flag_scratch.data(), n); break;
      }
      hash = fnv_bytes(hash, bytes, count);
      ok = write_block(fp, bytes, 1, count, blocks[block].name);
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
#ifdef NT9_AOS_PROBE
  throw std::runtime_error("AoS benchmark: model IO disabled");
#endif
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
      if (ok && block == 3) {
        for (size_t i = 0; i < count; i += sizeof(int)) {
          int flag;
          memcpy(&flag, buffer.data() + i, sizeof(flag));
          if (flag != 0 && flag != 1) { ok = false; break; }
        }
      }
      offset += count;
    }
    if (ok && hash != header.block_hashes[block]) {
      fprintf(stderr, "checksum mismatch in full training state %s block %s\n",
              filename, blocks[block].name);
      ok = false;
    }
  }
  if (ok) ok = fseeko(fp, payload_offset, SEEK_SET) == 0;
  if (ok) compact_initialize(progress_oi::enabled() ? 0.0 : static_cast<double>(EV_INIT_VALUE));
  for (const auto& block : blocks) {
    if (!ok) break;
    ok = block.id == 3 ? compact_read<int>(fp, block.id, FULL_STATE_COUNT)
                       : compact_read<double>(fp, block.id, FULL_STATE_COUNT);
  }
  if (fclose(fp) != 0) ok = false;
  if (!ok) {
    fprintf(stderr, "failed to load full training state: %s\n", filename);
    return false;
  }
  printf("loaded full training state from %s\n", filename);
  return true;
}

