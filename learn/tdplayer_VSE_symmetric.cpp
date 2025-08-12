#include <cstdio>
#include <random>
using namespace std;
#include "../head/game2048.h"
#include "../head/symmetric.h"
#include "../head/tdplayer_VSE_symmetric.h"
#include "../head/util.h"
#include <random>
using namespace std;
// #include "game2048.h"
// #include "symmetric.h"
// #include "tdplayer_VSE_symmetric.h"
// #include "util.h"

#define VARIATION_TILE 7
// #define TUPLE_SIZE 6
// #define NUM_TUPLE 9
#define NUM_STAGES 2
#define NUM_SPLIT 4
// #define NUM_TUPLE 10
#define ARRAY_LENGTH (VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE)

#define EV_INIT_VALUE 320000

// ステージ判定用閾値（この値以上の数字があるかでステージを決定）
#define STAGE_THRESHOLD 14

// デバッグフラグ (1にするとデバッグ情報を表示)
#define DEBUG_FILTERED_BOARDS 0

#ifndef TUPLE_FILE_TYPE
#define TUPLE_FILE_TYPE 6
#endif

#if TUPLE_FILE_TYPE == 6
#include "../head/selected_6_tuples.h"
#elif TUPLE_FILE_TYPE == 7
#include "../head/selected_7_tuples.h"
#elif TUPLE_FILE_TYPE == 8
#include "../head/selected_8_tuples.h"
#elif TUPLE_FILE_TYPE == 9
#include "../head/selected_9_tuples.h"
#else
#error "Invalid TUPLE_FILE_TYPE specified"
#endif

int Evs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int pos[NUM_TUPLE][TUPLE_SIZE];

//#include "selected_7_tuples.h"

int posSym[NUM_TUPLE][8][TUPLE_SIZE];

void output_ev(int seed,int suffix) {
  char filename[1024];
  FILE *fp;
  
  // datディレクトリを作成（既に存在する場合は何もしない）
  system("mkdir -p dat");
  
  sprintf(filename, "dat/tuples%d-NUM_TUPLE%d-seed%d-VSE-count%d.dat", TUPLE_SIZE, NUM_TUPLE, seed,suffix);
  fp = fopen(filename, "wb");
  fwrite(Evs, sizeof(int)*ARRAY_LENGTH, NUM_STAGES*NUM_SPLIT*NUM_TUPLE, fp);
  fclose(fp);

}

// ステージ判定関数：STAGE_THRESHOLD以上の値があるかでステージを決定
inline int get_stage(const board_t &board) {
  for (int i = 0; i < 16; i++) {
    if (board[i] >= STAGE_THRESHOLD) {
      return 1; // 高ステージ
    }
  }
  return 0; // 低ステージ
}

// --- フィルター関数群（void型＋出力引数） ---
void get_filtered_board_first(const board_t &board, board_t &filtered) {
  for (int i = 0; i < 16; ++i){
    if(board[i] == 0){
        filtered[i] = 0;
    }
    else if(board[i] > 5){
        filtered[i] = 6;
    }
    else{
        filtered[i] = board[i];
    }
  }
}

void get_filtered_board_second(const board_t &board, board_t &filtered) {
  for (int i = 0; i < 16; ++i){
    if(board[i] == 0){
        filtered[i] = 0;
    }
    else if(board[i] <= 5){
        filtered[i] = 1;
    }
    else if(board[i] > 9){
        filtered[i] = 6;
    }
    else{
        filtered[i] = board[i]-4;
    }
  }
}

void get_filtered_board_third(const board_t &board, board_t &filtered) {
  for (int i = 0; i < 16; ++i){
    if(board[i] == 0){
        filtered[i] = 0;
    }
    else if(board[i] <= 9){
        filtered[i] = 1;
    }
    else if(board[i] > 13){
        filtered[i] = 6;
    }
    else{
        filtered[i] = board[i]-8;
    }
  }
}

// 4つ目のフィルター
void get_filtered_board_fourth(const board_t &board, board_t &filtered) {
  for (int i = 0; i < 16; ++i){
    if(board[i] == 0){
        filtered[i] = 0;
    }
    else if(board[i] < 14){
        filtered[i] = 1;
    }
    else{
        filtered[i] = board[i]-12;
    }
  }
}

// フィルター関数ポインタの配列
typedef void (*FilterFunction)(const board_t &, board_t &);
FilterFunction filterFunctions[NUM_SPLIT] = {
  get_filtered_board_first,
  get_filtered_board_second,
  get_filtered_board_third,
  get_filtered_board_fourth
};

// デバッグ用: フィルターをかけた盤面とその評価値を表示
void debugFilteredBoards(const board_t &board) {
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用
  for (int f = 0; f < NUM_SPLIT; f++) {
    filterFunctions[f](board, filtered_boards[f]);
  }
  
  printf("=== Debug Filtered Boards ===\n");
  
  // 元のボード表示
  printf("Original board:\n");
  for (int i = 0; i < 16; i++) {
    printf("%2d ", board[i]);
    if (i % 4 == 3) printf("\n");
  }
  
  // 各フィルター後のボード表示
  const char* filter_names[NUM_SPLIT] = {"Upper", "Middle", "Lower", "Fourth"};
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
  
  for (int i = 0; i < NUM_TUPLE; i++) {
    for (int j = 0; j < 8; j++) {
      for (int f = 0; f < NUM_SPLIT; f++) {
        int index = 0;
        for (int k = 0; k < TUPLE_SIZE; k++) {
          const int tile = min(filtered_boards[f][posSym[i][j][k]], VARIATION_TILE);
          index = index * VARIATION_TILE + tile;
        }
        ev_filters[f] += Evs[stage][f][i][index];
      }
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
  
  // 各フィルターを適用
  for (int f = 0; f < NUM_SPLIT; f++) {
    filterFunctions[f](board, filtered_boards[f]);
  }
  
  for (int i = 0; i < NUM_TUPLE; i++) {
    for (int j = 0; j < 8; j++) {
      for (int f = 0; f < NUM_SPLIT; f++) {
        int index = 0;
        for (int k = 0; k < TUPLE_SIZE; k++) {
          const int tile = min(filtered_boards[f][posSym[i][j][k]], VARIATION_TILE);
          index = index * VARIATION_TILE + tile;
        }
        ev += Evs[stage][f][i][index];
      }
    }
  }
  return ev;
}
// デバッグ用: 指定した盤面の詳細情報を表示
void debugBoardInfo(const board_t &board) {
  debugFilteredBoards(board);
}

void init_tuple(char** argv) {
  int order[AVAIL_TUPLE];
  for (int i = 0; i < NUM_TUPLE; i++) {
    // order[i] = atoi(argv[2+i]);
    order[i] = i;
  }
  for (int i = 0; i < NUM_TUPLE; i++) {
    for (int k = 0; k < TUPLE_SIZE; k++) {
      pos[i][k] = tuples[order[i]][k];
    }
  }
  
  printf("TDPlayer 2 Symmetric ver.\n");
  for (int i = 0; i < NUM_TUPLE; i++) {
    printf(" %2d-th-tuples: %03d [%2d", (i+1), order[i], pos[i][0]);
    for (int j = 1; j < TUPLE_SIZE; j++) printf(",%2d", pos[i][j]);
    printf("]\n");
  }

  // 対称形の位置 posSym の計算
  for (int i = 0; i < NUM_TUPLE; i++) {
    for (int j = 0; j < 8; j++) {
      for (int k = 0; k < TUPLE_SIZE; k++) {
	posSym[i][j][k] = symmetricPos[j][pos[i][k]];
      }
    }
  }
  
  // 評価値の初期化: 全て0で初期化
  for (int s = 0; s < NUM_STAGES; s++) {
    for (int sp = 0; sp < NUM_SPLIT; sp++) {
      for (int i = 0; i < NUM_TUPLE; i++) {
        for (int j = 0; j < ARRAY_LENGTH; j++) {
          Evs[s][sp][i][j] = EV_INIT_VALUE;
        }
      }
    }
  }
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

inline int getIndexSym(const board_t &board, int tuple_id, int sym_id) {
  int index = 0;                 // 現在のボードのタプルのインデックス
  for (int j = 0; j < TUPLE_SIZE; j++) {
    const int tile = min(board[posSym[tuple_id][sym_id][j]], VARIATION_TILE);
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
  for (int i = 0; i < NUM_TUPLE; i++) {
    tiles[i] = index % VARIATION_TILE; index /= VARIATION_TILE;
  }
  int smaller = 0;
  for (int i = NUM_TUPLE - 1; i >= 0; i--) {
    smaller = smaller * VARIATION_TILE + (tiles[i] > 0 ? (tiles[i] - 1) : 0);
  }
  return smaller;
}

// （全ての対称形を考慮）対称形でのタプルの評価値合計を返す
inline int calcEv(const board_t &board)
{
  int ev = 0;
  int stage = get_stage(board);
  for (int k = 0; k < NUM_TUPLE; k++) { // タプルごとのループ
    for (int sym = 0; sym < 8; sym++) { // 対称変換ごとのループ
      const int index = getIndexSym(board, k, sym);
      if (Evs[stage][0][k][index] == EV_INIT_VALUE) {
        // 評価値が未定義のタイルの場合は評価値を更新
        const int smaller_index = getSmallerIndex(index);
        Evs[stage][0][k][index] = Evs[stage][0][k][smaller_index];
      }
      ev += Evs[stage][0][k][index]; // 最終的な評価値に加算
    }
  }
  return ev;
}

// 学習のための関数２
static void learningUpdate(const board_t& before, int delta)
{
  // NUM_SPLITつのフィルターで分割したので、deltaをNUM_SPLITで割る
  int stage_delta = delta / NUM_SPLIT;
  int stage = get_stage(before);
  
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用
  for (int f = 0; f < NUM_SPLIT; f++) {
    filterFunctions[f](before, filtered_boards[f]);
  }
  
  for (int k = 0; k < NUM_TUPLE; k++) { // タプルごとのループ
    for (int sym = 0; sym < 8; sym++) { // 対称変換ごとのループ
      for (int f = 0; f < NUM_SPLIT; f++) { // フィルターごとのループ
        int index = 0;
        for (int j = 0; j < TUPLE_SIZE; j++) {
          const int tile = min(filtered_boards[f][posSym[k][sym][j]], VARIATION_TILE);
          index = index * VARIATION_TILE + tile;
        }
        Evs[stage][f][k][index] += stage_delta;
      }
    }
  }
}  
void learning(const board_t &before, const board_t &after, int rewards)
{
  const int thisEvV = calcEvFiltered(before);
  const int nextEvV = calcEvFiltered(after);
  const int delta = rewards + ((nextEvV - thisEvV) >> 10); // V(S)とV'(S)の差分（1/1024単位）
  
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
  const int delta = 0 + ((0 - thisEvV) >> 10); // V(S)とV'(S)の差分（1/1024単位）
  learningUpdate(before, delta);
}

void TDPlayer::gameStart()
{
  firstTurn = true;
}

enum move_dir TDPlayer::selectHand(const board_t &/* board */,
				   const alldir_bool &canMoves,
				   const alldir_board &nextBoards,
				   const alldir_int &scores)
{
  // 評価値の計算（フィルター合計版）
  int nextEv[4];
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
  
  // 一手目以外は学習
  if (firstTurn) {
    firstTurn = false;
  } else {
    learning(lastBoard, nextBoards[selected], scores[selected]);
  }

  // lastBoardを更新
  for (int i = 0; i < 16; i++) {
    lastBoard[i] = nextBoards[selected][i];
  }

  return (enum move_dir)selected;
  
}

void TDPlayer::gameEnd()
{
  learningLast(lastBoard);
}

// #if 0
// // test code
// int main() {
//   init_movetable();
//   init_tuple();
//   printf("initialization finished\n");

//   return 0;
// }
// #endif
