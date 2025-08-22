#include <cstdio>
#include <random>
#include <cstring>
using namespace std;
#include "../head/game2048.h"
#include "../head/symmetric.h"
#include "../head/tdplayer_VSE_symmetric2.h"
#include "../head/util.h"
#include "../head/fixed_q10.hpp"
#include <random>
using namespace std;

#define NUM_STAGES 2
#define EV_INIT 320000
#define UC_CLIP 10
#define SIFT 10

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

int Evs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
float Errs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
float Aerrs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int Updatecounts[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
// int Evs_save[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int pos[NUM_TUPLE][TUPLE_SIZE];

//#include "selected_7_tuples.h"

void output_ev(int seed,int suffix) {
  char filename[1024];
  FILE *fp;

  system("mkdir -p dat");
  sprintf(filename, "dat/tuples%d-seed%d-VSE-count%03d.dat", TUPLE_FILE_TYPE, seed, suffix);
  fp = fopen(filename, "wb");
  if (!fp) { perror("fopen"); return; }

  // タプルごとに ARRAY_LENGTH 個を書き出す（Evs_save を使わない）
  // 注意: ARRAY_LENGTH が大きければ動的確保にする
  int *buf = (int*)malloc(sizeof(int) * ARRAY_LENGTH);
  if (!buf) { fclose(fp); return; }

  for (int s = 0; s < NUM_STAGES; s++) {
    for (int f = 0; f < NUM_SPLIT; f++) {
      for (int t = 0; t < NUM_TUPLE; t++) {
        // まずゼロで初期化（元コードの未設定エントリは 0 だった想定）
        memset(buf, 0, sizeof(int) * ARRAY_LENGTH);

        for (int a = 0; a < ARRAY_LENGTH; a++) {
          // Aerrs が 0 のときはチェックして除算を避ける
	  buf[a] = Evs[s][f][t][a] * Updatecounts[s][f][t][a]/(UC_CLIP)+(-EV_INIT)*(UC_CLIP-Updatecounts[s][f][t][a])/UC_CLIP;
        }

        size_t written = fwrite(buf, sizeof(int), ARRAY_LENGTH, fp);
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

// (filterFunctions removed; using mapping and apply_filters_from_mapping)

// デバッグ用: フィルターをかけた盤面とその評価値を表示
void debugFilteredBoards(const board_t &board) {
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用 (mapping 方式)
  apply_filters_from_mapping(board, filtered_boards);
  
  printf("=== Debug Filtered Boards ===\n");
  
  // 元のボード表示
  printf("Original board:\n");
  for (int i = 0; i < 16; i++) {
    printf("%2d ", board[i]);
    if (i % 4 == 3) printf("\n");
  }
  
  // 各フィルター後のボード表示
  const char* filter_names[4] = {"First", "Second", "Third", "Fourth"};
  for (int f = 0; f < NUM_SPLIT; f++) {
    printf("\nFiltered board (%s):\n", filter_names[f]);
    for (int i = 0; i < 16; i++) {
      printf("%2d ", filtered_boards[f][i]);
      if (i % 4 == 3) printf("\n");
    }
  }
  
  // 各フィルターの評価値を計算して表示
  int ev_filters[NUM_SPLIT] = {0};
  int stage = get_stage(board);
  
  for (int i = 0; i < UNROLL_COUNT; i++) {
      for (int f = 0; f < NUM_SPLIT; f++) {
        int index = 0;
        for (int k = 0; k < TUPLE_SIZE; k++) {
          // const int tile = min(filtered_boards[f][posSym[i][k]], VARIATION_TILE);
          index = index * VARIATION_TILE + filtered_boards[f][posSym[i][k]];
        }
        ev_filters[f] += Evs[stage][f][i%SYMMETRIC_TUPLE_NUM][index];
      }
  }
  
  printf("\nEvaluation values:\n");
  int total_ev = 0;
  for (int f = 0; f < NUM_SPLIT; f++) {
    printf("%s filter: %d\n", filter_names[f], ev_filters[f]);
    total_ev += ev_filters[f];
  }
  printf("Total: %d\n", total_ev);
  printf("==============================\n\n");
}

// フィルターで評価値を合計する関数
int calcEvFiltered(const board_t &board) {
  int ev = 0;
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
  int val = Evs[stage][f][base][index];
#if DEBUG_CALC_EV
  static int dbg_filters_printed = 0;
  if (dbg_filters_printed < 200) {
    printf("[calcEvFiltered] i=%d f=%d base=%d index=%d val=%d\n", i, f, base, index, val);
    fflush(stdout);
    dbg_filters_printed++;
  }
#endif
  ev += val;
      }
    // }
  }
  return ev;
}
// デバッグ用: 指定した盤面の詳細情報を表示
void debugBoardInfo(const board_t &board) {
  debugFilteredBoards(board);
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
  int n = 1;
  n <<= SIFT;
  fill_n(&Evs[0][0][0][0], EvsCount, (EV_INIT_VALUE * n));
  fill_n(&Errs[0][0][0][0], EvsCount, 0);
  fill_n(&Aerrs[0][0][0][0], EvsCount, 0);
  fill_n(&Updatecounts[0][0][0][0], EvsCount, 0);
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

inline int getIndex(const board_t &board, int tuple_id) {
  int index = 0;                 // 現在のボードのタプルのインデックス
  UNROLL_PRAGMA(TUPLE_SIZE)
  for (int j = 0; j < TUPLE_SIZE; j++) {
    const int tile = min(board[posSym[tuple_id][j]], VARIATION_TILE);
    index = index * VARIATION_TILE + tile;
  }
  if (index < 0 || index >= ARRAY_LENGTH) {
    printf("Error index: %d\n", index);
  }
  return index;
}

inline int getSmallerIndex(int index)
{
  int tiles[NUM_TUPLE];
  UNROLL_PRAGMA(NUM_TUPLE)
  for (int i = 0; i < NUM_TUPLE; i++) {
    tiles[i] = index % VARIATION_TILE;
    index /= VARIATION_TILE;
  }
  int smaller = 0;
  UNROLL_PRAGMA(NUM_TUPLE)
  for (int i = NUM_TUPLE - 1; i >= 0; i--) {
    smaller = smaller * VARIATION_TILE + (tiles[i] > 0 ? (tiles[i] - 1) : 0);
  }
  return smaller;
}

// 学習のための関数２
static void learningUpdate(const board_t& before, int delta)
{
  // 差分をタプル数,フィルター数で割る
  double stage_delta = q10_to_double(delta);
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
        if (Aerrs[stage][f][base][index] == 0) {
          Evs[stage][f][base][index] += q10_raw_from_double(stage_delta);
        } else {
          Evs[stage][f][base][index] += q10_raw_from_double_trunc(stage_delta * (abs(Errs[stage][f][base][index]) / Aerrs[stage][f][base][index]));
        }
        //AerrsとErrsは小数点なし
        float stage_delta_float = (float)stage_delta;
        Aerrs[stage][f][base][index] += fabs(stage_delta_float);
        Errs[stage][f][base][index]  += stage_delta_float;
        Updatecounts[stage][f][base][index] = min(UC_CLIP,Updatecounts[stage][f][base][index]+1);
      }
    // }
  }
}

void learning(const board_t &before, const board_t &after, int rewards)
{
  const int thisEvV = calcEvFiltered(before);
  const int nextEvV = calcEvFiltered(after);
  const int delta = (rewards << SIFT) + (nextEvV - thisEvV); // V(S)とV'(S)の差分（1/1024単位）
  
#if DEBUG_FILTERED_BOARDS
  printf("=== Learning Debug Info ===\n");
  printf("Rewards: %d, Delta: %d\n", rewards, delta);
  printf("Before state:\n");
  debugFilteredBoards(before);
  printf("After state:\n");
  debugFilteredBoards(after);
#endif
  
  learningUpdate(before, delta);
}  
void learningLast(const board_t &before)
{
  const int thisEvV = calcEvFiltered(before);
  const int delta = (0 << SIFT) + (0 - thisEvV); // V(S)とV'(S)の差分（1/1024単位）
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
  int nextEv[4] = {0,0,0,0};
  UNROLL_PRAGMA(4)
  for (int i = 0; i < 4; i++) {   // 方向ごと
    if (!canMoves[i]) continue; // 移動できない場合はスキップ
    nextEv[i] = calcEvFiltered(nextBoards[i]) + (scores[i] << 10);
  }
  
#if DEBUG_FILTERED_BOARDS
  printf("=== Hand Selection Debug Info ===\n");
  for (int i = 0; i < 4; i++) {
    if (!canMoves[i]) continue;
    printf("Direction %d (score: %d, ev: %d):\n", i, scores[i], nextEv[i]);
    debugFilteredBoards(nextBoards[i]);
  }
#endif
  
  // 最大の評価値を選択
  int maxi = -1; int maxv = 0;
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
  // // 一手目以外は学習
  // if (firstTurn) {
  //   firstTurn = false;
  // } else {
  //   UNROLL_PRAGMA(16) for (int i = 0; i < 16; i++) train_before[train_count][i] = lastBoard[i];
  //   UNROLL_PRAGMA(16) for (int i = 0; i < 16; i++) train_after[train_count][i] = nextBoards[selected][i];
  //   train_score[train_count] = scores[selected];
  //   train_count++;
  //   // learning(lastBoard, nextBoards[selected], scores[selected]);
  // }

  // // lastBoardを更新
  // UNROLL_PRAGMA(16)
  // for (int i = 0; i < 16; i++) {
  //   lastBoard[i] = nextBoards[selected][i];
  // }

  return (enum move_dir)selected;
  
}

void TDPlayer::gameEnd()
{
  // learningLast(lastBoard);

  // for (int i = train_count; i > 0; i--) {
  //   learning(train_before[i-1], train_after[i-1], train_score[i-1]);
  // }
  // train_count = 0;
  learningLast(train_after[--train_count]);
  for (; train_count > 0; --train_count) {
    learning(train_after[train_count - 1], train_after[train_count], train_score[train_count - 1]);
  }
}
