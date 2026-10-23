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

#ifndef TUPLE_FILE_TYPE
#define TUPLE_FILE_TYPE 6
#endif
class DoubleTupleEvaluator {
 public:
  static constexpr std::size_t kStages = 2;
  static constexpr int kTupleType = TUPLE_FILE_TYPE;
#if TUPLE_FILE_TYPE == 6
  static constexpr std::size_t kSplits=1, kTuples=9, kTileVariations=18, kTupleSize=6;
#elif TUPLE_FILE_TYPE == 7
  static constexpr std::size_t kSplits=2, kTuples=8, kTileVariations=11, kTupleSize=7;
#elif TUPLE_FILE_TYPE == 8
  static constexpr std::size_t kSplits=3, kTuples=5, kTileVariations=9, kTupleSize=8;
#elif TUPLE_FILE_TYPE == 9
  static constexpr std::size_t kSplits=4, kTuples=7, kTileVariations=7, kTupleSize=9;
#else
#error "Supported tuple types: 6, 7, 8, 9"
#endif
  static constexpr std::size_t kArrayLength = [] {
    std::size_t count=1;
    for (std::size_t i=0;i<kTupleSize;++i) count*=kTileVariations;
    return count;
  }();
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

// Compatibility for existing 6-tuple callers; new code uses the generic name.
using DoubleTupleEvaluator6 = DoubleTupleEvaluator;

}  // namespace beam2048

#endif  // DOUBLE_EVALUATOR_H
