/**
 * Player based on TC-learning with N-tuple networks.
 * Detail: TC(lambda), backward (offline) learning, symmetric sampling.
 *         Reduced memory usage to support 8-tuple 8-stages in 12GB
 */

#ifndef __TC_BACKWARD_SYMMETRIC_PLAYER_S_H__
#define __TC_BACKWARD_SYMMETRIC_PLAYER_S_H__

#include "game2048.h"
#include "symmetric.h"
#include "util.h"
#include "tuples6_sym.h"

void init_ev();
void input_ev(const char* filename);
void output_ev(const char* filename);

/*
 * A player based on TC-learning with N-tuple networks.
 */
class TDPlayer {
public:
  TDPlayer();
  virtual ~TDPlayer();

  int learn_start;
  bool learningActive;
  int turn;
  board_t *bd_history; // board
  int     *sc_history; // score
  int     *mv_history; // move
  int     *st_history; // stage

  void learnStart();
  void gameStart();

  // random play
  enum move_dir selectHandRandom(const board_t &board,
				 const alldir_bool &canMoves,
				 const alldir_board &nextBoards,
				 const alldir_int &scores,
				 mt19937 &mt);

  // greedy play
  enum move_dir selectHand(const board_t &board,
			   const alldir_bool &canMoves,
			   const alldir_board &nextBoards,
			   const alldir_int &scores);

  // expectimax play
  enum move_dir selectHandExpectimax(const board_t &board,
				     const alldir_bool &canMoves,
				     const alldir_board &nextBoards,
				     const alldir_int &scores,
				     int depth);
  void learnEnd();
  void gameEnd();
};

#endif // __TD_FORWARD_SYMMETRIC_PLAYER_S_H__
