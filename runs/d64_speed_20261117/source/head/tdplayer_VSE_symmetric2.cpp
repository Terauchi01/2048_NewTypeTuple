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
int pos[NUM_TUPLE][TUPLE_SIZE];

void input_ev(const char* filename) {
  FILE *fp;

  fp = fopen(filename, "rb");
  if (!fp) { perror("fopen"); return; }

  // タプルごとに ARRAY_LENGTH 個を読み込む
  // 注意: ARRAY_LENGTH が大きければ動的確保にする
  int *buf = (int*)malloc(sizeof(int) * ARRAY_LENGTH);
  if (!buf) { fclose(fp); return; }

  for (int s = 0; s < NUM_STAGES; s++) {
    for (int f = 0; f < NUM_SPLIT; f++) {
      for (int t = 0; t < NUM_TUPLE; t++) {
	size_t read = fread(Evs[s][f][t], sizeof(int), ARRAY_LENGTH, fp);
        if (read != (size_t)ARRAY_LENGTH) {
          perror("fread");
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
	ev += val;
      }
  }
  return ev;
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

void TDPlayer::gameStart()
{
  firstTurn = true;
}

int expectimaxPlay(int depth, const board_t& board)
{
  if (depth <= 1) { return calcEvFiltered(board); }

  int count = 0;
  long long sumScore = 0;
  // すべての場所に置いてみる
  for (int index = 0; index < 16; index++) {
    if (board[index] != 0) continue;
    count++;
    board_t putBoard; board_t nextBoard; int score;
    for (int i = 0; i < 16; i++) putBoard[i] = board[i];
    putBoard[index] = 1;
    int maxScore = -100000;
    for (int d = 0; d < 4; d++) {
      score = moveB(putBoard, nextBoard, (enum move_dir)d);
      if (score == -1) continue;
      int sc = expectimaxPlay(depth-1, nextBoard) + (score << 10);
      printf("depth %d index %d num 1 d %d ==> %d\n", depth-1, index, d, sc);
      if (maxScore < sc) maxScore = sc;
    }
    sumScore += maxScore * 9;

    putBoard[index] = 2;
    maxScore = -100000;
    for (int d = 0; d < 4; d++) {
      score = moveB(putBoard, nextBoard, (enum move_dir)d);
      if (score == -1) continue;
      int sc = expectimaxPlay(depth-1, nextBoard) + (score << 10);
      printf("depth %d index %d num 2 d %d ==> %d\n", depth-1, index, d, sc);
      if (maxScore < sc) maxScore = sc;
    }
    sumScore += maxScore;
  }
  if (count == 0) return 0;
  return sumScore / 10 / count;
}

enum move_dir TDPlayer::selectHandExpectimax(const board_t &/* board */,
					     const alldir_bool &canMoves,
					     const alldir_board &nextBoards,
					     const alldir_int &scores,
					     int depth)
{
  // 評価値の計算（フィルター合計版）
  int nextEv[4] = {0,0,0,0};
  for (int i = 0; i < 4; i++) {   // 方向ごと
    if (!canMoves[i]) continue; // 移動できない場合はスキップ
    nextEv[i] = expectimaxPlay(depth, nextBoards[i]) + (scores[i] << 10);
  }
  printf("nextEvs: %d %d %d %d\n", nextEv[0], nextEv[1], nextEv[2], nextEv[3]);
  
  // 最大の評価値を選択
  int maxi = -1; int maxv = 0;
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
  printf("selected %d\n", selected);
  return (enum move_dir)selected;
}
void TDPlayer::gameEnd()
{
}
