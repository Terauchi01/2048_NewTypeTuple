#ifndef START_BOARD_POOL_H
#define START_BOARD_POOL_H

#include "game2048.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// CSV v1 stores post-move, pre-spawn afterstates, with exponents in c0..c15.
// Format: # start_pool_v1, then # key=value metadata for representation=afterstate,
// source_policy, source_run, selection=all|success|failure, and
// generation=first_16k_afterstate. Header: board_id,source_game,source_turn,
// cumulative_score,c0,...,c15. IDs are unquoted CSV fields; numbers are decimal.
// The declaration records supplied provenance, not independent proof of it.
// init_movetable() must run before Pool::load(). A pool start must survive every
// immediate spawn: an empty observed trajectory cannot be passed to gameEnd().
namespace start_pool {
enum class Mode { empty, pool16k };
inline Mode parse_mode(const std::string& value) {
  if (value == "empty") return Mode::empty;
  if (value == "pool16k") return Mode::pool16k;
  throw std::invalid_argument("start mode must be empty or pool16k");
}
inline const char* mode_name(Mode mode) {
  if (mode == Mode::empty) return "empty";
  if (mode == Mode::pool16k) return "pool16k";
  throw std::invalid_argument("invalid start mode");
}
struct Entry {
  std::string board_id, source_game;
  long long source_turn = 0;
  int cumulative_score = 0;
  std::array<int, 16> board{};
};
inline void copy_board(const Entry& entry, board_t& board) {
  std::copy(entry.board.begin(), entry.board.end(), board);
}
inline std::vector<std::string> split(const std::string& text) {
  std::vector<std::string> fields;
  std::stringstream input(text);
  std::string field;
  while (std::getline(input, field, ',')) fields.push_back(field);
  if (!text.empty() && text.back() == ',') fields.emplace_back();
  return fields;
}
inline long long integer(const std::string& text) {
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument("pool field must be a nonnegative integer");
  size_t used = 0;
  long long result = std::stoll(text, &used);
  if (used != text.size()) throw std::invalid_argument("invalid integer in pool");
  return result;
}
inline void validate(const Entry& entry) {
  if (entry.board_id.empty() || entry.source_game.empty() || entry.source_turn <= 0 || entry.cumulative_score < 0)
    throw std::invalid_argument("invalid pool row provenance");
  int empty = 0, maximum = 0, tile16k = 0;
  for (int cell : entry.board) {
    if (cell < 0 || cell > 17) throw std::invalid_argument("pool tile exponent outside 0..17");
    empty += cell == 0;
    tile16k += cell == 14;
    maximum = std::max(maximum, cell);
  }
  if (maximum != 14 || tile16k != 1 || empty == 0)
    throw std::invalid_argument("pool must contain 16k afterstates with an empty cell");
  for (int cell = 0; cell < 16; ++cell) {
    if (entry.board[cell] != 0) continue;
    for (int exponent : {1, 2}) {
      board_t spawned;
      copy_board(entry, spawned);
      spawned[cell] = exponent;
      bool legal = false;
      for (int direction = 0; direction < 4; ++direction) {
        board_t after;
        if (moveB(spawned, after, static_cast<move_dir>(direction)) >= 0) legal = true;
      }
      if (!legal) throw std::invalid_argument("pool start can terminate before its first action");
    }
  }
}
class Pool {
public:
  std::vector<Entry> entries;
  std::map<std::string, std::string> metadata;
  void load(const std::string& filename) {
    std::ifstream input(filename);
    if (!input) throw std::runtime_error("cannot open start pool: " + filename);
    std::string line;
    if (!std::getline(input, line)) throw std::invalid_argument("empty pool file");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "# start_pool_v1")
      throw std::invalid_argument("pool requires # start_pool_v1 marker");
    std::map<std::string, std::string> next_metadata;
    std::vector<Entry> next_entries;
    std::set<std::string> board_ids, game_ids;
    bool header_seen = false;
    std::string header = "board_id,source_game,source_turn,cumulative_score";
    for (int cell = 0; cell < 16; ++cell) header += ",c" + std::to_string(cell);
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) continue;
      if (line.rfind("# ", 0) == 0) {
        if (header_seen) throw std::invalid_argument("pool metadata must precede data");
        const auto equals = line.find('=', 2);
        if (equals == std::string::npos || equals == 2 || equals + 1 == line.size())
          throw std::invalid_argument("invalid pool metadata");
        if (!next_metadata.emplace(line.substr(2, equals - 2), line.substr(equals + 1)).second)
          throw std::invalid_argument("duplicate pool metadata");
        continue;
      }
      if (!header_seen) {
        if (line != header) throw std::invalid_argument("invalid pool CSV header");
        header_seen = true;
        continue;
      }
      const auto fields = split(line);
      if (fields.size() != 20) throw std::invalid_argument("pool rows need 20 fields");
      Entry entry;
      entry.board_id = fields[0]; entry.source_game = fields[1];
      entry.source_turn = integer(fields[2]);
      const long long score = integer(fields[3]);
      if (score > std::numeric_limits<int>::max()) throw std::invalid_argument("pool score overflows int");
      entry.cumulative_score = static_cast<int>(score);
      for (int cell = 0; cell < 16; ++cell) {
        const long long value = integer(fields[cell + 4]);
        if (value > 17) throw std::invalid_argument("pool exponent exceeds 17");
        entry.board[cell] = static_cast<int>(value);
      }
      validate(entry);
      if (!board_ids.insert(entry.board_id).second || !game_ids.insert(entry.source_game).second)
        throw std::invalid_argument("pool requires unique board and source-game identifiers");
      next_entries.push_back(entry);
    }
    for (const char* field : {"representation", "source_policy", "source_run", "selection", "generation"})
      if (!next_metadata.count(field) || next_metadata[field].empty())
        throw std::invalid_argument(std::string("missing pool metadata: ") + field);
    if (next_metadata["representation"] != "afterstate" || next_metadata["generation"] != "first_16k_afterstate")
      throw std::invalid_argument("pool must declare first_16k_afterstate generation and afterstate representation");
    if (next_metadata["selection"] != "all" && next_metadata["selection"] != "success" && next_metadata["selection"] != "failure")
      throw std::invalid_argument("pool selection must be all, success or failure");
    if (next_metadata.count("usable_for_learning") && next_metadata["usable_for_learning"] != "true")
      throw std::invalid_argument("pool is marked unusable for learning");
    if (!header_seen || next_entries.empty()) throw std::invalid_argument("start pool is empty");
    entries.swap(next_entries); metadata.swap(next_metadata);
  }
  template <class Engine> const Entry& draw(Engine& engine) const {
    if (entries.empty()) throw std::logic_error("cannot draw from empty pool");
    return entries[std::uniform_int_distribution<size_t>(0, entries.size() - 1)(engine)];
  }
};
} // namespace start_pool
#endif
