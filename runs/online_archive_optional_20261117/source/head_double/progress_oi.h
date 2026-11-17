#ifndef PROGRESS_OI_H
#define PROGRESS_OI_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "game2048.h"

namespace progress_oi {
#ifdef NT9_FLOAT32_STORAGE
using scalar_t = float;
#else
using scalar_t = double;
#endif

struct Interval {
  int begin;
  int end;
  scalar_t value;
  long long sample_count;
};

// Inline state makes this header usable by both the learning and evaluation
// executables without adding another link-time dependency.
inline std::vector<Interval> profile;
inline std::string profile_path;

inline std::string trim(const std::string& input) {
  const auto begin = std::find_if_not(
      input.begin(), input.end(),
      [](unsigned char c) { return std::isspace(c) != 0; });
  const auto end = std::find_if_not(
      input.rbegin(), input.rend(),
      [](unsigned char c) { return std::isspace(c) != 0; }).base();
  return begin < end ? std::string(begin, end) : std::string();
}

inline std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) fields.push_back(trim(field));
  if (!line.empty() && line.back() == ',') fields.emplace_back();
  return fields;
}

inline bool parse_int(const std::string& text, int* value) {
  try {
    size_t used = 0;
    const long long parsed = std::stoll(text, &used);
    if (used != text.size() || parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
      return false;
    }
    *value = static_cast<int>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

inline bool parse_count(const std::string& text, long long* value) {
  try {
    size_t used = 0;
    const long long parsed = std::stoll(text, &used);
    if (used != text.size() || parsed < 0) return false;
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

inline bool parse_double(const std::string& text, double* value) {
  try {
    size_t used = 0;
    const double parsed = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

inline bool parse_value(const std::string& text, scalar_t* value) {
  double parsed;
  if (!parse_double(text, &parsed)) return false;
  const scalar_t narrowed=static_cast<scalar_t>(parsed);
  if (!std::isfinite(narrowed)) return false;
  *value=narrowed;
  return true;
}

inline bool load(const char* filename) {
  std::ifstream input(filename);
  if (!input) {
    std::fprintf(stderr, "failed to open progress OI profile: %s\n", filename);
    return false;
  }

  std::string line;
  if (!std::getline(input, line)) {
    std::fprintf(stderr, "empty progress OI profile: %s\n", filename);
    return false;
  }
  if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef &&
      static_cast<unsigned char>(line[1]) == 0xbb &&
      static_cast<unsigned char>(line[2]) == 0xbf) {
    line.erase(0, 3);
  }
  const auto header = split_csv_line(line);
  int begin_column = -1;
  int end_column = -1;
  int value_column = -1;
  int count_column = -1;
  for (size_t i = 0; i < header.size(); ++i) {
    if (header[i] == "bin_start") begin_column = static_cast<int>(i);
    if (header[i] == "bin_end_exclusive") end_column = static_cast<int>(i);
    if (header[i] == "oi_value") value_column = static_cast<int>(i);
    if (header[i] == "sample_count") count_column = static_cast<int>(i);
  }
  if (begin_column < 0 || end_column < 0 || value_column < 0 ||
      count_column < 0) {
    std::fprintf(stderr,
                 "invalid progress OI header in %s; required columns are "
                 "bin_start,bin_end_exclusive,oi_value,sample_count\n",
                 filename);
    return false;
  }

  const int max_column =
      std::max(std::max(begin_column, end_column),
               std::max(value_column, count_column));
  std::vector<Interval> parsed_profile;
  int line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (trim(line).empty()) continue;
    const auto fields = split_csv_line(line);
    Interval interval{};
    if (static_cast<int>(fields.size()) <= max_column ||
        !parse_int(fields[begin_column], &interval.begin) ||
        !parse_int(fields[end_column], &interval.end) ||
        !parse_value(fields[value_column], &interval.value) ||
        !parse_count(fields[count_column], &interval.sample_count)) {
      std::fprintf(stderr, "invalid progress OI row at %s:%d\n", filename,
                   line_number);
      return false;
    }
    if (interval.begin < 0 || interval.end <= interval.begin) {
      std::fprintf(stderr, "invalid progress interval at %s:%d\n", filename,
                   line_number);
      return false;
    }
    if (parsed_profile.empty()) {
      if (interval.begin != 0) {
        std::fprintf(stderr,
                     "progress OI profile must start at zero: %s:%d\n",
                     filename, line_number);
        return false;
      }
    } else if (interval.begin != parsed_profile.back().end) {
      std::fprintf(stderr,
                   "progress OI intervals must be sorted and contiguous: "
                   "%s:%d\n",
                   filename, line_number);
      return false;
    }
    parsed_profile.push_back(interval);
  }
  if (parsed_profile.empty()) {
    std::fprintf(stderr, "progress OI profile has no data rows: %s\n", filename);
    return false;
  }

  profile = std::move(parsed_profile);
  profile_path = filename;
  std::fprintf(stderr,
               "progress OI enabled: profile=%s bins=%zu range=[0,%d); "
               "progress >= %d uses the last value %.17g\n",
               profile_path.c_str(), profile.size(), profile.back().end,
               profile.back().end, profile.back().value);
  return true;
}

inline bool enabled() { return !profile.empty(); }

inline int calculate_progress(const board_t& board) {
  long long tile_sum = 0;
  for (int i = 0; i < 16; ++i) {
    const int exponent = board[i];
    if (exponent <= 0) continue;
    if (exponent >= 62) return std::numeric_limits<int>::max();
    tile_sum += 1LL << exponent;
    if (tile_sum / 2 >= std::numeric_limits<int>::max()) {
      return std::numeric_limits<int>::max();
    }
  }
  return static_cast<int>(tile_sum / 2);
}

inline scalar_t value_for_progress(int progress) {
  if (profile.empty()) return scalar_t(0);
  if (progress >= profile.back().end) return profile.back().value;
  const auto found = std::upper_bound(
      profile.begin(), profile.end(), progress,
      [](int value, const Interval& interval) { return value < interval.begin; });
  return (found == profile.begin() ? profile.front() : *std::prev(found)).value;
}

inline scalar_t value(const board_t& board) {
  return value_for_progress(calculate_progress(board));
}

}  // namespace progress_oi

#endif  // PROGRESS_OI_H
