#ifndef __TDPLAYER2SYMMETRIC_H__
#define __TDPLAYER2SYMMETRIC_H__

#include <unordered_map>
using namespace std;

typedef bool alldir_bool[4];
typedef board_t alldir_board[4];
typedef int alldir_int[4];

void init_tuple();
void output_ev(int seed, int suffix);
void input_ev(const char* filename);
bool input_ev_for_learning(const char* filename);
bool output_learning_state(const char* dat_filename, long long step_count,
                           int loop_count, int last_log);
bool input_learning_state(const char* dat_filename, long long* step_count,
                          int* loop_count, int* last_log);

class TDPlayer {
  int train_count;
  board_t* train_before;
  board_t* train_after;
  int* train_score;
public:
  TDPlayer() {
    train_before = new board_t[100000];
    train_after = new board_t[100000];
    train_score = new int[100000];
  }
  
  board_t lastBoard;
  bool firstTurn;

  void gameStart();
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
