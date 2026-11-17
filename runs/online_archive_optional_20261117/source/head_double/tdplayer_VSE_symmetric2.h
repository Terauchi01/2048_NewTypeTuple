#ifndef __TDPLAYER2SYMMETRIC_H__
#define __TDPLAYER2SYMMETRIC_H__

#include <unordered_map>
#include <cstddef>
using namespace std;

typedef bool alldir_bool[4];
typedef board_t alldir_board[4];
typedef int alldir_int[4];

void init_tuple();
void output_ev(int seed, int suffix);
std::size_t evaluation_checkpoint_bytes();
void input_ev(const char* filename);
bool input_ev_for_learning(const char* filename);
bool output_learning_state(const char* dat_filename, long long step_count,
                           int loop_count, int last_log);
bool input_learning_state(const char* dat_filename, long long* step_count,
                          int* loop_count, int* last_log);
// Full model/TC state, distinct from the clipped evaluation .dat export.
// Call only while all learning workers are quiescent. The caller owns RNG,
// episode/counter metadata and must treat a false return as a fatal error.
bool save_full_training_state(const char* filename);
bool load_full_training_state(const char* filename);
#ifdef NT9_FLOAT32_STORAGE
using td_value_t = float;
#else
using td_value_t = double;
#endif
td_value_t calcEvFiltered(const board_t& board);
bool load_progress_oi_profile(const char* filename);
bool progress_oi_is_enabled();
td_value_t progress_oi_baseline(const board_t& board);

class TDPlayer {
  int train_count;
  board_t* train_before;
  board_t* train_after;
  int* train_score;
  // Transient first-segment archive connection, never checkpointed mid-episode.
  board_t archive_source_afterstate_{};
  bool archive_source_pending_ = false;
  std::size_t archive_source_updates_ = 0;
public:
  TDPlayer() {
    train_before = new board_t[100000];
    train_after = new board_t[100000];
    train_score = new int[100000];
  }
  
  board_t lastBoard;
  bool firstTurn;

  void gameStart();
  // Register the source before the first random tile is spawned. Does not add
  // a fictitious move to the normal/cached action history.
  void set_archive_source_afterstate(const board_t& board);
  std::size_t archive_source_update_count() const { return archive_source_updates_; }
  enum move_dir selectHand(const board_t &board,
			   const alldir_bool &canMoves,
			   const alldir_board &nextBoards,
			   const alldir_int &scores);
  enum move_dir selectHandExpectimax(const board_t &board,
				     const alldir_bool &canMoves,
				     const alldir_board &nextBoards,
				     const alldir_int &scores,
				     int depth,
				     unordered_map<unsigned long long, double>& ev_table);
  void gameEnd();
};

#endif
