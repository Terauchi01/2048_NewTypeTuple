#ifndef DOUBLE_EVALUATOR_H
#define DOUBLE_EVALUATOR_H

#include <cstddef>
#include <memory>
#include <string>

#include "beam_search.h"

namespace beam2048 {

struct DoubleBuffer {
  std::unique_ptr<double[]> values;
  std::size_t size = 0;

  double* data() { return values.get(); }
  const double* data() const { return values.get(); }
  bool empty() const { return size == 0; }
  void clear() {
    values.reset();
    size = 0;
  }
};

// Reads exactly expected_count native-endian doubles from a raw file or an
// xz stream. On failure, output is empty and error (when non-null) describes
// the problem. The xz process must also exit successfully, which verifies its
// integrity check rather than accepting a truncated decompression stream.
bool load_exact_doubles(const std::string& path, std::size_t expected_count,
                        DoubleBuffer& output, std::string* error = nullptr);

class DoubleTupleEvaluator6 {
 public:
  static constexpr std::size_t kStages = 2;
  static constexpr std::size_t kSplits = 1;
  static constexpr std::size_t kTuples = 9;
  static constexpr std::size_t kTileVariations = 18;
  static constexpr std::size_t kTupleSize = 6;
  static constexpr std::size_t kArrayLength =
      kTileVariations * kTileVariations * kTileVariations *
      kTileVariations * kTileVariations * kTileVariations;
  static constexpr std::size_t kValueCount =
      kStages * kSplits * kTuples * kArrayLength;
  static constexpr std::size_t kByteCount = kValueCount * sizeof(double);

  bool load(const std::string& path, std::string* error = nullptr);
  bool loaded() const { return table_.size == kValueCount; }

  // Thread-safe after a successful load. Throws std::logic_error when called
  // before loading a complete evaluator table.
  double evaluate(const Board& board) const;

 private:
  DoubleBuffer table_;
};

}  // namespace beam2048

#endif  // DOUBLE_EVALUATOR_H
