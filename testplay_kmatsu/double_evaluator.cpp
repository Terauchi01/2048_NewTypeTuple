#include "double_evaluator.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "../head/selected_6_tuples_sym.h"

namespace beam2048 {
namespace {

struct InputStream {
  FILE* file = nullptr;
  bool pipe = false;
};

bool ends_with(const std::string& text, const char* suffix) {
  const std::size_t suffix_size = std::strlen(suffix);
  return text.size() >= suffix_size &&
         text.compare(text.size() - suffix_size, suffix_size, suffix) == 0;
}

std::string shell_quote(const std::string& text) {
  std::string result("'");
  for (const char character : text) {
    if (character == '\'') {
      result += "'\\''";
    } else {
      result.push_back(character);
    }
  }
  result.push_back('\'');
  return result;
}

InputStream open_stream(const std::string& path) {
  if (ends_with(path, ".xz")) {
    const std::string command = "xz -dc -- " + shell_quote(path);
    return {popen(command.c_str(), "r"), true};
  }
  return {std::fopen(path.c_str(), "rb"), false};
}

bool close_stream(InputStream stream) {
  if (stream.file == nullptr) return false;
  return stream.pipe ? pclose(stream.file) == 0
                     : std::fclose(stream.file) == 0;
}

void set_error(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

std::size_t table_offset(int stage, int tuple, int index) {
  return (static_cast<std::size_t>(stage) * DoubleTupleEvaluator6::kTuples +
          static_cast<std::size_t>(tuple)) *
             DoubleTupleEvaluator6::kArrayLength +
         static_cast<std::size_t>(index);
}

}  // namespace

bool load_exact_doubles(const std::string& path, std::size_t expected_count,
                        DoubleBuffer& output, std::string* error) {
  output.clear();
  if (error != nullptr) error->clear();
  if (path.empty()) {
    set_error(error, "evaluation file path is empty");
    return false;
  }
  if (expected_count >
      std::numeric_limits<std::size_t>::max() / sizeof(double)) {
    set_error(error, "evaluation element count overflows byte size");
    return false;
  }

  std::error_code size_error;
  const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    set_error(error, "cannot inspect evaluation file: " +
                         size_error.message());
    return false;
  }
  if (!ends_with(path, ".xz")) {
    const std::uintmax_t expected_bytes =
        static_cast<std::uintmax_t>(expected_count) * sizeof(double);
    if (file_size != expected_bytes) {
      set_error(error, "evaluation file size mismatch: expected " +
                           std::to_string(expected_bytes) + ", got " +
                           std::to_string(file_size) + " bytes");
      return false;
    }
  }

  InputStream stream = open_stream(path);
  if (stream.file == nullptr) {
    set_error(error, "cannot open evaluation file: " +
                         std::string(std::strerror(errno)));
    return false;
  }

  std::unique_ptr<double[]> candidate;
  if (expected_count != 0) {
    candidate.reset(new (std::nothrow) double[expected_count]);
    if (!candidate) {
      set_error(error, "cannot allocate " + std::to_string(expected_count) +
                           " double evaluation values");
      close_stream(stream);
      return false;
    }
  }

  const std::size_t count =
      std::fread(candidate.get(), sizeof(double), expected_count, stream.file);
  if (count != expected_count) {
    const bool read_error = std::ferror(stream.file) != 0;
    close_stream(stream);
    set_error(error, read_error
                         ? "failed while reading evaluation values"
                         : "evaluation file is truncated: expected " +
                               std::to_string(expected_count) + ", read " +
                               std::to_string(count) + " doubles");
    return false;
  }

  unsigned char trailing_byte = 0;
  if (std::fread(&trailing_byte, 1, 1, stream.file) != 0) {
    close_stream(stream);
    set_error(error, "evaluation file contains trailing data");
    return false;
  }
  if (std::ferror(stream.file)) {
    close_stream(stream);
    set_error(error, "failed while checking the end of evaluation file");
    return false;
  }
  if (!close_stream(stream)) {
    set_error(error,
              "evaluation stream failed its close/integrity check (xz may be "
              "corrupt)");
    return false;
  }

  output.values = std::move(candidate);
  output.size = expected_count;
  return true;
}

bool DoubleTupleEvaluator6::load(const std::string& path, std::string* error) {
  table_.clear();
  return load_exact_doubles(path, kValueCount, table_, error);
}

double DoubleTupleEvaluator6::evaluate(const Board& board) const {
  if (!loaded()) {
    throw std::logic_error("double 6-tuple evaluator is not loaded");
  }

  int stage = 0;
  for (const int tile : board) {
    if (tile >= 14) {
      stage = 1;
      break;
    }
  }

  double result = 0.0;
  for (int symmetric_tuple = 0; symmetric_tuple < UNROLL_COUNT;
       ++symmetric_tuple) {
    int index = 0;
    for (int tile_index = 0; tile_index < TUPLE_SIZE; ++tile_index) {
      const int board_index = posSym[symmetric_tuple][tile_index];
      const int tile = std::clamp(board[board_index], 0, 17);
      index = index * VARIATION_TILE + tile;
    }
    const int tuple = symmetric_tuple % NUM_TUPLE;
    result += table_.data()[table_offset(stage, tuple, index)];
  }
  return result;
}

}  // namespace beam2048
