#include <cstdio>
#include <random>
#include <cstring>
using namespace std;
#include "game2048.h"
#include "symmetric.h"
#include "tdplayer_VSE_symmetric2.h"
#include "util.h"
// #include "../head/fixed_q10.hpp"
#include <random>
using namespace std;

#define NUM_STAGES 2
#define EV_INIT 320000
#define UC_CLIP 1
// #define ALPHA 0.5
extern double ALPHA;

// ステージ判定用閾値（この値以上の数字があるかでステージを決定）
#define STAGE_THRESHOLD 14

// デバッグフラグ (1にするとデバッグ情報を表示)
#define DEBUG_FILTERED_BOARDS 0

// calcEv デバッグ出力 (1 にするとログを出す)
#ifndef DEBUG_CALC_EV
#define DEBUG_CALC_EV 0
#endif

#ifndef TUPLE_FILE_TYPE
#define TUPLE_FILE_TYPE 6
#endif

#define MAX_TILE_VALUE 17

#if TUPLE_FILE_TYPE == 6
#include "../head/selected_6_tuples_sym.h"
static const uint8_t filter_mapping[1][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 }
};
// #define DEFAULT_UNROLL_COUNT 72
#elif TUPLE_FILE_TYPE == 0
#include "../head/selected_6G_tuples_sym.h"
static const uint8_t filter_mapping[1][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 }
};
// #define DEFAULT_UNROLL_COUNT 64
#elif TUPLE_FILE_TYPE == 7
#include "../head/selected_7_tuples_sym.h"
static const uint8_t filter_mapping[2][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9 }
};
// #define DEFAULT_UNROLL_COUNT 64
#elif TUPLE_FILE_TYPE == 8
#include "../head/selected_8_tuples_sym.h"
static const uint8_t filter_mapping[3][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5 }
};
// #define DEFAULT_UNROLL_COUNT 40
#elif TUPLE_FILE_TYPE == 9
#include "../head/selected_9_tuples_sym.h"
static const uint8_t filter_mapping[4][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5 }
};
// #define DEFAULT_UNROLL_COUNT 56
#else
#error "Invalid TUPLE_FILE_TYPE specified"
static const uint8_t filter_mapping[4][18] = {
  { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6 },
  { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5 }
};
#endif

// --- _Pragma ラッパ ---
// 文字列化
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#if defined(__CUDACC__)
#define UNROLL_PRAGMA(N) _Pragma(STR(unroll N))                     // NVCC
#elif defined(__clang__)
#define UNROLL_PRAGMA(N) _Pragma(STR(clang loop unroll_count(N)))   // Clang / AppleClang
#elif defined(__GNUC__)
#define UNROLL_PRAGMA(N) _Pragma(STR(GCC unroll N))                 // GCC
#else
#define UNROLL_PRAGMA(N) /* fallback: 何もしない */
#endif

// apply mapping to produce NUM_SPLIT filtered boards
static inline void apply_filters_from_mapping(const board_t &board, board_t filtered_boards[NUM_SPLIT]) {
  UNROLL_PRAGMA(NUM_SPLIT*16)
    for (int fi = 0; fi < NUM_SPLIT*16; ++fi) {
      int f = fi/16;
      int i = fi%16; 
      int v = board[i];
      filtered_boards[f][i] = filter_mapping[f][v];
    }
}

double Evs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
double Errs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
double Aerrs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int Updatecounts[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int pos[NUM_TUPLE][TUPLE_SIZE];

void resetErrs() {
  constexpr size_t EvsCount = size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;
  fill_n(&Errs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Aerrs[0][0][0][0], EvsCount, 1e-12);
}

void output_ev(int seed,int suffix) {
  char filename[1024];
  FILE *fp;

  int r = system("mkdir -p dat"); if (r != 0) return;
  sprintf(filename, "dat/tuples%d-seed%d-VSE-count%03d.dat", TUPLE_FILE_TYPE, seed, suffix);
  fp = fopen(filename, "wb");
  if (!fp) { perror("fopen"); return; }

  // タプルごとに ARRAY_LENGTH 個を書き出す（Evs_save を使わない）
  // 注意: ARRAY_LENGTH が大きければ動的確保にする
  double *buf = (double*)malloc(sizeof(double) * ARRAY_LENGTH);
  if (!buf) { fclose(fp); return; }

  for (int s = 0; s < NUM_STAGES; s++) {
    for (int f = 0; f < NUM_SPLIT; f++) {
      for (int t = 0; t < NUM_TUPLE; t++) {
        // まずゼロで初期化（元コードの未設定エントリは 0 だった想定）
        memset(buf, 0, sizeof(double) * ARRAY_LENGTH);

        for (int a = 0; a < ARRAY_LENGTH; a++) {
	  if (Updatecounts[s][f][t][a] < UC_CLIP) {
	    buf[a] = 0;
	  } else {
	    buf[a] = Evs[s][f][t][a];
	  }
        }

        size_t written = fwrite(buf, sizeof(double), ARRAY_LENGTH, fp);
        if (written != (size_t)ARRAY_LENGTH) {
          perror("fwrite");
          free(buf);
          fclose(fp);
          return;
        }
      }
    }
  }

  free(buf);
  fclose(fp);
}

// ステージ判定関数：STAGE_THRESHOLD以上の値があるかでステージを決定
inline int get_stage(const board_t &board) {
  UNROLL_PRAGMA(16)
    for (int i = 0; i < 16; i++) {
      if (board[i] >= STAGE_THRESHOLD) {
	return 1; // 高ステージ
      }
    }
  return 0; // 低ステージ
}

// フィルターで評価値を合計する関数
double calcEvFiltered(const board_t &board) {
  double ev = 0;
  int stage = get_stage(board);
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用 (mapping 方式)
  apply_filters_from_mapping(board, filtered_boards);
  UNROLL_PRAGMA(UNROLL_COUNT)
    for (int i = 0; i < UNROLL_COUNT; i++) {
      UNROLL_PRAGMA(NUM_SPLIT)
	for (int f = 0; f < NUM_SPLIT; f++) {
	  int index = 0;
	  UNROLL_PRAGMA(TUPLE_SIZE)
	    for (int k = 0; k < TUPLE_SIZE; k++) {
	      index = index * VARIATION_TILE + filtered_boards[f][posSym[i][k]];
	    }
	  int base = i % NUM_TUPLE;
	  double val = Evs[stage][f][base][index];
	  ev += val;
	}
    }
  return ev;
}

void init_tuple() {
  // build_filter_mapping();
  printf("TDPlayer 2 Symmetric ver.\n");
  for (int i = 0; i < NUM_TUPLE; i++) {
    printf(" %2d-th-tuples: [%2d", (i+1),tuples[i][0]);
    for (int j = 1; j < TUPLE_SIZE; j++) printf(",%2d", tuples[i][j]);
    printf("]\n");
  }
  // 評価値の初期化: 全て0で初期化
  constexpr size_t EvsCount = size_t(NUM_STAGES) * NUM_SPLIT * NUM_TUPLE * ARRAY_LENGTH;
  fill_n(&Evs[0][0][0][0], EvsCount, (double)(EV_INIT_VALUE));
  fill_n(&Errs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Aerrs[0][0][0][0], EvsCount, 1e-12);
  fill_n(&Updatecounts[0][0][0][0], EvsCount, 0);
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

// 学習のための関数２
static void learningUpdate(const board_t& before, double delta)
{
  // 差分をタプル数,フィルター数で割る
  double stage_delta = delta;
  stage_delta = stage_delta / (AVAIL_TUPLE * SYMMETRIC_TUPLE_NUM * NUM_SPLIT);

  int stage = get_stage(before);
  
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用 (mapping 方式)
  apply_filters_from_mapping(before, filtered_boards);
  UNROLL_PRAGMA(AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM)
    for (int k = 0; k < AVAIL_TUPLE*SYMMETRIC_TUPLE_NUM; k++) { // タプルごとのループ
      UNROLL_PRAGMA(NUM_SPLIT)
	for (int f = 0; f < NUM_SPLIT; f++) { // フィルターごとのループ
	  int index = 0;
	  UNROLL_PRAGMA(TUPLE_SIZE)
	    for (int j = 0; j < TUPLE_SIZE; j++) {
	      index = index * VARIATION_TILE + filtered_boards[f][posSym[k][j]];
	    }
	  int base = k % NUM_TUPLE;
	  Aerrs[stage][f][base][index] += fabs(stage_delta);
	  Errs[stage][f][base][index]  += stage_delta;
	  Evs[stage][f][base][index] += stage_delta * 0.5 * fabs(Errs[stage][f][base][index]) / Aerrs[stage][f][base][index];
	  Updatecounts[stage][f][base][index] = 1;
	}
    }
}

void learning(const board_t &before, const board_t &after, int rewards)
{
  const double thisEvV = calcEvFiltered(before);
  const double nextEvV = calcEvFiltered(after);
  const double delta = rewards + (nextEvV - thisEvV); // V(S)とV'(S)の差分（1/1024単位）
  
  learningUpdate(before, delta);
}  

 void learningLast(const board_t &before)
{
  const double thisEvV = calcEvFiltered(before);
  const double delta = 0 + (0 - thisEvV); // V(S)とV'(S)の差分（1/1024単位）
  learningUpdate(before, delta);
}

void TDPlayer::gameStart()
{
  firstTurn = true;
  train_count = 0;
}

enum move_dir TDPlayer::selectHand(const board_t &/* board */,
				   const alldir_bool &canMoves,
				   const alldir_board &nextBoards,
				   const alldir_int &scores)
{
  // 評価値の計算（フィルター合計版）
  double nextEv[4] = {0,0,0,0};
  UNROLL_PRAGMA(4)
    for (int i = 0; i < 4; i++) {   // 方向ごと
      if (!canMoves[i]) continue; // 移動できない場合はスキップ
      nextEv[i] = calcEvFiltered(nextBoards[i]) + scores[i];
    }
  
#if DEBUG_FILTERED_BOARDS
  printf("=== Hand Selection Debug Info ===\n");
  for (int i = 0; i < 4; i++) {
    if (!canMoves[i]) continue;
    printf("Direction %d (score: %d, ev: %f):\n", i, scores[i], nextEv[i]);
    debugFilteredBoards(nextBoards[i]);
  }
#endif
  
  // 最大の評価値を選択
  int maxi = -1; double maxv = 0;
  UNROLL_PRAGMA(4)
    for (int i = 0; i < 4; i++) { // 方向ごと
      if (!canMoves[i]) continue;
      if (maxi == -1) {
	maxi = i;
	maxv = nextEv[i];
      } else {
	if (nextEv[i] > maxv) {
	  maxi = i; maxv = nextEv[i];
	}
      }
    }
  const int selected = maxi;
  if (selected < 0 || selected >= 4) {
    printf("OUTOFRANGE: selected = %d\n", selected);
  }

  UNROLL_PRAGMA(16)
    for (int i = 0; i < 16; i++) {
      train_after[train_count][i] = nextBoards[selected][i];
    }
  train_score[train_count++] = scores[selected];

  // 一手目以外は学習
  if (firstTurn) {
    firstTurn = false;
  } else {
    // learning(lastBoard, nextBoards[selected], scores[selected]);
  }

  // // lastBoardを更新 ==> train_after配列に
  // UNROLL_PRAGMA(16)
  //   for (int i = 0; i < 16; i++) {
  //     lastBoard[i] = nextBoards[selected][i];
  //   }

  return (enum move_dir)selected;
  
}

void TDPlayer::gameEnd()
{
  // learningLast(lastBoard);

  learningLast(train_after[--train_count]);
  for (; train_count > 0; --train_count) {
    learning(train_after[train_count - 1], train_after[train_count], train_score[train_count - 1]);
  }
}
