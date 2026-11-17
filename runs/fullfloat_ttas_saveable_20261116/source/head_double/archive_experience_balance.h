#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

// Counts the driver's existing learning-step units, not scalar weight writes.
// One episode here means its original game plus all of its restart segments.
// No method holds this mutex while consulting or publishing an archive source.
namespace archive_experience_balance {

struct Snapshot {
  std::uint64_t normal_steps = 0, archive_steps = 0;
  std::uint64_t requested_normal = 0, requested_archive = 0;
  std::uint64_t normal_starts = 0, archive_starts = 0;
  std::uint64_t archive_fallbacks = 0;
  std::uint64_t normal_completed = 0, archive_completed = 0;
  std::uint64_t inflight_normal = 0, inflight_archive = 0, pending = 0;
  std::uint64_t tie_cursor = 0;
};

class Scheduler {
 public:
  explicit Scheduler(std::size_t workers = 0) {
    if (workers) configure(workers);
  }

  void configure(std::size_t workers) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!workers || workers > 65536)
      throw std::invalid_argument("invalid archive balance worker count");
    for (const auto value : fields(state_))
      if (value) throw std::logic_error("cannot reset active archive balance");
    for (const auto phase : workers_)
      if (phase != Phase::idle)
        throw std::logic_error("archive balance worker is active");
    workers_.assign(workers, Phase::idle);
  }

  // This combines choice and reservation. Do not call begin() afterwards.
  bool choose_steps(std::size_t worker) {
    std::lock_guard<std::mutex> lock(mutex_);
    require_idle(worker);
    bool archive;
    auto cursor = state_.tie_cursor;
    if (state_.archive_steps != state_.normal_steps) {
      archive = state_.archive_steps < state_.normal_steps;
    } else if (state_.inflight_archive != state_.inflight_normal) {
      archive = state_.inflight_archive < state_.inflight_normal;
    } else {
      archive = (cursor & 1U) != 0;  // First exact tie starts normally.
      cursor = add(cursor, 1);
    }
    begin_locked(worker, archive);
    state_.tie_cursor = cursor;
    return archive;
  }

  // Start-count/probability conditions make their choice externally, retaining
  // their existing RNG consumption, then reserve that requested origin here.
  void begin(std::size_t worker, bool requested_archive) {
    std::lock_guard<std::mutex> lock(mutex_);
    require_idle(worker);
    begin_locked(worker, requested_archive);
  }

  // Call after source acquisition. A failed archive request is a normal start.
  void commit(std::size_t worker, bool actual_archive) {
    std::lock_guard<std::mutex> lock(mutex_);
    require_worker(worker);
    const auto phase = workers_[worker];
    if (phase != Phase::reserved_normal && phase != Phase::reserved_archive)
      throw std::logic_error("archive balance start is not reserved");
    if (actual_archive && phase != Phase::reserved_archive)
      throw std::logic_error("unrequested archive start");
    Snapshot next = state_;
    --next.pending;
    if (actual_archive) {
      next.archive_starts = add(next.archive_starts, 1);
    } else {
      next.normal_starts = add(next.normal_starts, 1);
      if (phase == Phase::reserved_archive) {
        next.archive_fallbacks = add(next.archive_fallbacks, 1);
        --next.inflight_archive;
        next.inflight_normal = add(next.inflight_normal, 1);
      }
    }
    state_ = next;
    workers_[worker] = actual_archive ? Phase::archive : Phase::normal;
  }

  // Called after each gameEnd(), including every restart. It deliberately does
  // not touch the learner's global completed counter or its TC schedule.
  void record_segment(std::size_t worker, std::int64_t steps) {
    std::lock_guard<std::mutex> lock(mutex_);
    require_worker(worker);
    const auto phase = workers_[worker];
    if (steps < 0 || (phase != Phase::normal && phase != Phase::archive))
      throw std::logic_error("invalid archive balance segment");
    const auto count = static_cast<std::uint64_t>(steps);
    const auto normal = phase == Phase::normal
        ? add(state_.normal_steps, count) : state_.normal_steps;
    const auto archive = phase == Phase::archive
        ? add(state_.archive_steps, count) : state_.archive_steps;
    (void)add(normal, archive);
    state_.normal_steps = normal;
    state_.archive_steps = archive;
  }

  void end_episode(std::size_t worker) {
    std::lock_guard<std::mutex> lock(mutex_);
    require_worker(worker);
    const auto phase = workers_[worker];
    if (phase != Phase::normal && phase != Phase::archive)
      throw std::logic_error("archive balance episode is not committed");
    if (phase == Phase::archive) {
      const auto completed = add(state_.archive_completed, 1);
      --state_.inflight_archive;
      state_.archive_completed = completed;
    } else {
      const auto completed = add(state_.normal_completed, 1);
      --state_.inflight_normal;
      state_.normal_completed = completed;
    }
    workers_[worker] = Phase::idle;
  }

  Snapshot snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  void assert_quiescent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    quiescent_locked();
  }

  void save(std::ostream& output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    quiescent_locked();
    output << "ARCHIVE_EXPERIENCE_BALANCE_V1 " << workers_.size();
    for (const auto value : fields(state_)) output << ' ' << value;
    output << '\n';
    if (!output) throw std::runtime_error("archive balance save failed");
  }

  // Loading is transactional and requires the caller's configured worker count.
  // A saved wave is quiescent: no partial source reservations are serialized.
  void load(std::istream& input) {
    std::lock_guard<std::mutex> lock(mutex_);
    quiescent_locked();
    std::string line;
    if (!std::getline(input >> std::ws, line))
      throw std::runtime_error("missing archive balance state");
    std::istringstream row(line);
    std::string magic, token;
    if (!(row >> magic) || magic != "ARCHIVE_EXPERIENCE_BALANCE_V1" ||
        !(row >> token) || number(token) != workers_.size())
      throw std::runtime_error("archive balance schema/worker mismatch");
    std::array<std::uint64_t, 13> values{};
    for (auto& value : values) {
      if (!(row >> token)) throw std::runtime_error("truncated archive balance state");
      value = number(token);
    }
    if (row >> token) throw std::runtime_error("extra archive balance fields");
    Snapshot next{values[0], values[1], values[2], values[3], values[4],
                  values[5], values[6], values[7], values[8], values[9],
                  values[10], values[11], values[12]};
    validate_quiescent(next);
    state_ = next;
  }

 private:
  enum class Phase { idle, reserved_normal, reserved_archive, normal, archive };
  static constexpr std::uint64_t limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  mutable std::mutex mutex_;
  Snapshot state_;
  std::vector<Phase> workers_;

  static std::array<std::uint64_t, 13> fields(const Snapshot& s) {
    return {{s.normal_steps, s.archive_steps, s.requested_normal,
             s.requested_archive, s.normal_starts, s.archive_starts,
             s.archive_fallbacks, s.normal_completed, s.archive_completed,
             s.inflight_normal, s.inflight_archive, s.pending, s.tie_cursor}};
  }
  static std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (a > limit || b > limit || b > limit-a)
      throw std::overflow_error("archive balance counter overflow");
    return a+b;
  }
  static std::uint64_t number(const std::string& text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data()+text.size() || value > limit)
      throw std::runtime_error("invalid archive balance counter");
    return value;
  }
  static void validate_quiescent(const Snapshot& s) {
    for (const auto value : fields(s))
      if (value > limit) throw std::runtime_error("invalid archive balance counter");
    const auto requests = add(s.requested_normal, s.requested_archive);
    if (s.inflight_normal || s.inflight_archive || s.pending ||
        add(s.normal_starts, s.archive_starts) != requests ||
        add(s.requested_normal, s.archive_fallbacks) != s.normal_starts ||
        add(s.archive_starts, s.archive_fallbacks) != s.requested_archive ||
        s.normal_completed != s.normal_starts ||
        s.archive_completed != s.archive_starts || s.tie_cursor > requests ||
        (!s.normal_starts && s.normal_steps) || (!s.archive_starts && s.archive_steps))
      throw std::runtime_error("inconsistent archive balance state");
    (void)add(s.normal_steps, s.archive_steps);
  }
  void require_worker(std::size_t worker) const {
    if (worker >= workers_.size()) throw std::out_of_range("archive balance worker index");
  }
  void require_idle(std::size_t worker) const {
    require_worker(worker);
    if (workers_[worker] != Phase::idle)
      throw std::logic_error("archive balance worker already active");
  }
  void begin_locked(std::size_t worker, bool archive) {
    Snapshot next = state_;
    if (archive) {
      next.requested_archive = add(next.requested_archive, 1);
      next.inflight_archive = add(next.inflight_archive, 1);
    } else {
      next.requested_normal = add(next.requested_normal, 1);
      next.inflight_normal = add(next.inflight_normal, 1);
    }
    next.pending = add(next.pending, 1);
    (void)add(next.requested_normal, next.requested_archive);
    state_ = next;
    workers_[worker] = archive ? Phase::reserved_archive : Phase::reserved_normal;
  }
  void quiescent_locked() const {
    if (workers_.empty()) throw std::logic_error("archive balance is not configured");
    for (const auto phase : workers_)
      if (phase != Phase::idle)
        throw std::logic_error("archive balance checkpoint has active workers");
    validate_quiescent(state_);
  }
};
}  // namespace archive_experience_balance
