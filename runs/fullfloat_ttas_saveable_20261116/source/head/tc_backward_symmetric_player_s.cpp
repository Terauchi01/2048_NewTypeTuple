/**
 * Player based on TC-learning with N-tuple networks.
 * Detail: TC(lambda), backward (offline) learning, symmetric sampling.
 *         Reduced memory usage to support 8-tuple 8-stages in 12GB
 */

#include <cstdio>
#include <cstring>
#include <random>
using namespace std;
#include "tc_backward_symmetric_player_s.h"
#include "util.h"

#define EV_INIT_VALUE 0

#define NUM_STAGE 8
#define STAGED_BY13
#define WITH_STAGEPROMOTION

#define NUM_S_STAGES 4
#define NUM_L_STAGES 4

// File-Local variables
int     Evs_s[NUM_S_STAGES][NUM_TUPLE][POW_15_6];
float  Sums_s[NUM_S_STAGES][NUM_TUPLE][POW_15_6]; // sum of errors
float ASums_s[NUM_S_STAGES][NUM_TUPLE][POW_15_6]; // sum of absolute errors

int     Evs_l[NUM_L_STAGES][NUM_TUPLE][POW_16_6];
float  Sums_l[NUM_L_STAGES][NUM_TUPLE][POW_16_6]; // sum of errors
float ASums_l[NUM_L_STAGES][NUM_TUPLE][POW_16_6]; // sum of absolute errors

// File-Local functions
inline int calcEv(const board_t &board, int stage);
inline int calcEv_s(const board_t &board, int stage);
inline int calcEv_l(const board_t &board, int stage);
static void learningUpdate(const board_t& before, int stage, int delta);
static void learningUpdate_s(const board_t& before, int stage, int delta);
static void learningUpdate_l(const board_t& before, int stage, int delta);
inline int computeStage(const board_t &board, int turn);

//======================================================================
#include "player_common2.cpp"

// void input_ev(const char* filename)
// {
//   FILE *fp = fopen(filename, "rb");
//   if (fp == NULL) { printf("read error: %s\n", filename); exit(1); }
//   int readcount = fread(Evs, sizeof(int) * NUM_STAGE * ARRAY_LENGTH, NUM_TUPLE, fp);
//   if (readcount != NUM_TUPLE) { printf("read size error: %s\n", filename); exit(1); }
//   fclose(fp);
//   printf("Finish reading Ev values from %s\n", filename);
// }

// void output_ev(const char* filename) {
//   FILE *fp = fopen(filename, "wb");
//   if (fp == NULL) { printf("write error: %s\n", filename); exit(1); }
//   fwrite(Evs, sizeof(int) * NUM_STAGE * ARRAY_LENGTH, NUM_TUPLE, fp);
//   fclose(fp);
//   printf("Finish writing Ev values to %s\n", filename);
// }

void init_ev() {
  for (int i = 0; i < NUM_S_STAGES; i++) {
    for (int j = 0; j < NUM_TUPLE; j++) {
      for (int k = 0; k < POW_15_6; k++) {
	Evs_s  [i][j][k] = EV_INIT_VALUE;
	Sums_s [i][j][k] = 0;
	ASums_s[i][j][k] = 0;
      }
    }
  }
  for (int i = 0; i < NUM_L_STAGES; i++) {
    for (int j = 0; j < NUM_TUPLE; j++) {
      for (int k = 0; k < POW_16_6; k++) {
	Evs_l  [i][j][k] = EV_INIT_VALUE;
	Sums_l [i][j][k] = 0;
	ASums_l[i][j][k] = 0;
      }
    }
  }
}

// (すべての相似な盤面を含む)ある盤面でのタプルの出力の合計を返す
inline int calcEv(const board_t &board, int stage)
{
  if (stage < NUM_S_STAGES) return calcEv_s(board, stage);
  return calcEv_l(board, stage - NUM_S_STAGES);
}

int calcEv_s(const board_t &board, int stage)
{
  int ev = 0;
  for (int k = 0; k < NUM_TUPLE; k++) { // タプル毎のループ
    for (int sym = 0; sym < 8; sym++) {
      const int index = getIndexSym(board, k, sym, 15);
      if (Evs_s[stage][k][index] == EV_INIT_VALUE && stage > 0) {
	ev += Evs_s[stage-1][k][index]; // STAGE_PROMOTION
      } else {
	ev += Evs_s[stage][k][index];
      }
    }
  }
  return ev;
}

int calcEv_l(const board_t &board, int stage)
{
  int ev = 0;
  for (int k = 0; k < NUM_TUPLE; k++) { // タプル毎のループ
    for (int sym = 0; sym < 8; sym++) {
      const int index = getIndexSym(board, k, sym, 16);
      if (Evs_l[stage][k][index] == EV_INIT_VALUE) {
	if (stage > 0) {
	  ev += Evs_l[stage-1][k][index]; // STAGE_PROMOTION
	} else {
	  const int index15 = getIndexSym(board, k, sym, 15);
	  ev += Evs_s[NUM_S_STAGES-1][k][index15]; // STAGE_PROMOTION
	}
      } else {
	ev += Evs_l[stage][k][index];
      }
    }
  }
  return ev;
}

// 学習のための関数群
static void learningUpdate(const board_t& before, int stage, int error)
{
  if (stage < NUM_S_STAGES) return learningUpdate_s(before, stage, error);
  return learningUpdate_l(before, stage - NUM_S_STAGES, error);
}

static void learningUpdate_s(const board_t& before, int stage, int error)
{
  for (int k = 0; k < NUM_TUPLE; k++) { // タプル毎のループ
    for (int sym = 0; sym < 8; sym++) { // 回転や対称な盤面毎のループ
      const int index = getIndexSym(before, k, sym, 15);
      double alpha = (ASums_s[stage][k][index] == 0) ? 1.0
		      : (fabs(Sums_s[stage][k][index]) / ASums_s[stage][k][index]);
      Sums_s[stage][k][index] += error;
      ASums_s[stage][k][index] += abs(error);
      Evs_s[stage][k][index] += (int)(error / 8 / NUM_TUPLE * alpha);
    }
  }
}

static void learningUpdate_l(const board_t& before, int stage, int error)
{
  for (int k = 0; k < NUM_TUPLE; k++) { // タプル毎のループ
    for (int sym = 0; sym < 8; sym++) { // 回転や対称な盤面毎のループ
      const int index = getIndexSym(before, k, sym, 16);
      double alpha = (ASums_l[stage][k][index] == 0) ? 1.0
		      : (fabs(Sums_l[stage][k][index]) / ASums_l[stage][k][index]);
      Sums_l[stage][k][index] += error;
      ASums_l[stage][k][index] += abs(error);
      Evs_l[stage][k][index] += (int)(error / 8 / NUM_TUPLE * alpha);
    }
  }
}

TDPlayer::TDPlayer()
{
  learn_start = 0;
  learningActive = true;
  turn = 0;
  bd_history = new board_t[100000];
  sc_history = new int    [100000];
  mv_history = new int    [100000];
  st_history = new int    [100000];
}

TDPlayer::~TDPlayer()
{
  delete [] bd_history;
  delete [] sc_history;
  delete [] mv_history;
  delete [] st_history;
}

void TDPlayer::gameStart()
{
  turn = 0;
}

// random play
enum move_dir TDPlayer::selectHandRandom(const board_t &board,
					 const alldir_bool &canMoves,
					 const alldir_board &nextBoards,
					 const alldir_int &scores,
					 mt19937 &mt)
{
  int nextStages[4];
  int canMoveCount = 0; int canMoveIndex[4];
  for (int i = 0; i < 4; i++) {   // 方向毎
    if (!canMoves[i]) continue; // 動かせなければ無視
    nextStages[i] = computeStage(nextBoards[i], turn);
    canMoveIndex[canMoveCount++] = i;
  }

  int selected = canMoveIndex[randN(mt, canMoveCount)];
  copyBoard(nextBoards[selected], bd_history[turn]);
  sc_history[turn] = scores[selected];
  st_history[turn] = nextStages[selected];
  turn++;

  return (enum move_dir)selected;
}

enum move_dir TDPlayer::selectHand(const board_t &/* board */,
				   const alldir_bool &canMoves,
				   const alldir_board &nextBoards,
				   const alldir_int &scores)
{
  int nextStages[4];
  int selected = -1; int maxv = 0;
  // 評価値の計算と最大の評価値を選ぶ
  for (int i = 0; i < 4; i++) {   // 方向毎
    if (!canMoves[i]) continue; // 動かせなければ無視
    nextStages[i] = computeStage(nextBoards[i], turn);
    const int ev = calcEv(nextBoards[i], nextStages[i]) + (scores[i] << 10);
    if (selected == -1 || ev > maxv) {
	selected = i; maxv = ev;
    }
  }

  // 選んだ手によって学習のために保存する
  copyBoard(nextBoards[selected], bd_history[turn]);
  sc_history[turn] = scores[selected];
  st_history[turn] = nextStages[selected];
  turn++;

  return (enum move_dir)selected;
}

enum move_dir TDPlayer::selectHandExpectimax(const board_t &/* board */,
					     const alldir_bool &canMoves,
					     const alldir_board &nextBoards,
					     const alldir_int &scores,
					     int depth)
{
  int nextStages[4];
  int selected = -1; int maxv = 0;
  // 評価値の計算と最大の評価値を選ぶ
  for (int i = 0; i < 4; i++) {   // 方向毎
    if (!canMoves[i]) continue; // 動かせなければ無視
    nextStages[i] = computeStage(nextBoards[i], turn);
    const int ev = expectimaxPlay(depth-1, nextBoards[i], turn) + (scores[i] << 10);
    if (selected == -1 || ev > maxv) {
	selected = i; maxv = ev;
    }
  }

  // 選んだ手によって学習のために保存する
  copyBoard(nextBoards[selected], bd_history[turn]);
  sc_history[turn] = scores[selected];
  st_history[turn] = nextStages[selected];
  turn++;
  return (enum move_dir)selected;
}

void TDPlayer::gameEnd()
{
  // まとめて学習する
  if (!learningActive) return;

  int t = turn-1;
  int error = ((0 << 10) + (0 - calcEv(bd_history[t], st_history[t])));
  learningUpdate(bd_history[t], st_history[t], error);

  for (t--; t >= learn_start; t--) {
    error = ((sc_history[t+1] << 10)
	     + (calcEv(bd_history[t+1], st_history[t+1])
		- calcEv(bd_history[t], st_history[t])));
    learningUpdate(bd_history[t], st_history[t], error);
  }
}
