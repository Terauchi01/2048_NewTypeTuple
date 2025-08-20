// g++ Play_perfect_player.cevals -std=c++20 -mcmodel=large -O2
#include <array>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <list>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>
using namespace std;
namespace fs = std::filesystem;
#include "../head/game2048.h"
#include "../head/symmetric.h"
#include "../head/tdplayer_VSE_symmetric2.h"
#include "../head/util.h"

// tdplayer functions used from tdplayer_VSE_symmetric2.cpp
extern int calcEvFiltered(const board_t &board);

#define NUM_STAGES 2
#define STAGE_THRESHOLD 14

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
  #include "../head/selected_6_tuples_sym.h"
  static const uint8_t filter_mapping[1][18] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 }
  };
#endif

// External array from tdplayer implementation
extern int Evs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];

class GameOver {
 public:
  int gameover_turn;
  int game;
  int progress;
  int score;
  GameOver(int gameover_turn_init, int game_init, int progress_init,
           int score_init)
      : gameover_turn(gameover_turn_init),
        game(game_init),
        progress(progress_init),
        score(score_init) {}
};
int progress_calculation(int board[9]) {
  int sum = 0;
  for (int i = 0; i < 9; i++) {
    if (board[i] != 0) {
      sum += 1 << board[i];
    }
  }
  return sum / 2;
}

// ファイル名からパラメータを抽出する構造体と関数を追加
struct FileParams {
  int NT;
  int tupleNumber;
  int multiStaging;
  int oi;
  int seed;
  int c;
  int mini;
};

FileParams parseFileName(const char* filename) {
  FileParams params = {0, 0, 0, 0, 0, 0, 0};
  char basename[256];
  strcpy(basename, filename);

  // ファイルパスから最後の'/'以降を取得
  char* last_slash = strrchr(basename, '/');
  char* actual_name = last_slash ? last_slash + 1 : basename;

  // 最初の数字を取得してNTに設定
  params.NT = atoi(actual_name);

  // .zip 拡張子を削除
  char* ext = strstr(basename, ".zip");
  if (ext) *ext = '\0';

  // .dat 拡張子を削除
  ext = strstr(basename, ".dat");
  if (ext) *ext = '\0';

  char* token = strtok(basename, "_");
  while (token != NULL) {
    if (strncmp(token, "TupleNumber", 11) == 0)
      params.tupleNumber = atoi(token + 11);
    else if (strncmp(token, "Multistaging", 12) == 0)
      params.multiStaging = atoi(token + 12);
    else if (strncmp(token, "OI", 2) == 0)
      params.oi = atoi(token + 2);
    else if (strncmp(token, "seed", 4) == 0)
      params.seed = atoi(token + 4);
    else if (strncmp(token, "c", 1) == 0)
      params.c = atoi(token + 1);
    else if (strncmp(token, "mini", 4) == 0)
      params.mini = atoi(token + 4);

    token = strtok(NULL, "_");
  }
  return params;
}

bool read_ev(const char* evfile) {
  FILE *fp;

  fp = fopen(evfile, "rb");
  if (!fp) {
    perror("fopen for reading");
    return false;
  }

  // タプルごとに ARRAY_LENGTH 個を読み込む
  int *buf = (int*)malloc(sizeof(int) * ARRAY_LENGTH);
  if (!buf) {
    fclose(fp);
    return false;
  }

  for (int s = 0; s < NUM_STAGES; s++) {
    for (int f = 0; f < NUM_SPLIT; f++) {
      for (int t = 0; t < NUM_TUPLE; t++) {
        size_t read_items = fread(buf, sizeof(int), ARRAY_LENGTH, fp);
        if (read_items != (size_t)ARRAY_LENGTH) {
          if (feof(fp)) {
            printf("Unexpected end of file while reading stage=%d, filter=%d, tuple=%d\\n", s, f, t);
          } else {
            perror("fread");
          }
          free(buf);
          fclose(fp);
          return false;
        }

        // バッファから Evs 配列にコピー
        for (int a = 0; a < ARRAY_LENGTH; a++) {
          Evs[s][f][t][a] = buf[a];
        }
      }
    }
  }

  free(buf);
  fclose(fp);
  printf("Successfully loaded evaluation values from %s\\n", evfile);
  return true;
}

int main(int argc, char** argv) {
  // Suppress unused variable warnings
  (void)filter_mapping;
  
  if (argc < 2 + 1) {
    fprintf(stderr, "Usage: playgreedy <seed> <game_counts> <evfile>\n");
    exit(1);
  }
  int seed = atoi(argv[1]);
  int game_count = atoi(argv[2]);
  char* evfile = argv[3];
  // string number(1, evfile[12]);
  FileParams params = parseFileName(evfile);
  fs::create_directory("../board_data");
  string dir = "../board_data/NT" + to_string(params.NT) + "_TN" +
               to_string(params.tupleNumber) + "_OI" + to_string(params.oi) +
               "_seed" + to_string(params.seed) + "_mini" + to_string(params.mini) + "/";
  fs::create_directory(dir);
  fs::path abs_path = fs::absolute(dir);

  // FILE* fp = fopen(evfile, "rb");
  FILE* test_fp = fopen(evfile, "rb");
  if (!test_fp) {
    fprintf(stderr, "Error: Cannot open file %s\n", evfile);
    exit(1);
  }
  fclose(test_fp);

  if (!read_ev(evfile)) {
    fprintf(stderr, "Failed to load evaluation file: %s\n", evfile);
    exit(1);
  }
  // FILE* fp = fopen(evfile, "rb");
  srand(seed);
  list<array<int, 9>> state_list;
  list<array<int, 9>> after_state_list;
  const int eval_length = 5;
  list<array<double, eval_length>> eval_list;
  list<GameOver> GameOver_list;
  // Simple greedy play using TDPlayer and game2048 core functions
  for (int gid = 1; gid <= game_count; gid++) {
    board_t board;
    for (int i = 0; i < 16; i++) board[i] = 0;
    int turn = 0;
    int game_score = 0;

    // place two initial tiles (like game start)
    std::mt19937 mt(rand());
    for (int k = 0; k < 2; k++) {
      int p = -1;
      int empty[16]; int ec = 0;
      for (int i = 0; i < 16; i++) if (board[i] == 0) empty[ec++] = i;
      if (ec > 0) p = empty[rand() % ec];
      if (p >= 0) board[p] = (rand() % 10 == 0) ? 2 : 1;
    }

    while (true) {
      turn++;
      // prepare nextBoards and scores
      alldir_bool canMoves;
      alldir_board nextBoards;
      alldir_int scores;
      for (int d = 0; d < 4; d++) {
        scores[d] = moveB(board, nextBoards[d], (enum move_dir)d);
        canMoves[d] = (scores[d] > -1);
      }

      // pick best move
      int selected = -1; int bestv = INT_MIN;
      for (int d = 0; d < 4; d++) {
        if (!canMoves[d]) continue;
        int ev = calcEvFiltered(nextBoards[d]) + (scores[d] << 10);
        if (selected == -1 || ev > bestv) { selected = d; bestv = ev; }
      }

      if (selected < 0) {
        // no moves -> game over
        GameOver_list.push_back(GameOver(turn, gid, progress_calculation(board), game_score));
        break;
      }

      // save pre-move state
      state_list.push_back(array<int,9>{board[0],board[1],board[2],board[3],board[4],board[5],board[6],board[7],board[8]});

      // apply move
      for (int i = 0; i < 16; i++) board[i] = nextBoards[selected][i];
      game_score += scores[selected];

      // record after-state
      after_state_list.push_back(array<int,9>{board[0],board[1],board[2],board[3],board[4],board[5],board[6],board[7],board[8]});
      eval_list.push_back(array<double, eval_length>{(double)0,0,0,0,(double)progress_calculation(board)});

      // add random tile
      int empty[16]; int ec = 0;
      for (int i = 0; i < 16; i++) if (board[i] == 0) empty[ec++] = i;
      if (ec > 0) {
        int p = empty[rand() % ec];
        board[p] = (rand() % 10 == 0) ? 2 : 1;
      }
    }
  }
  // printf("average = %f\n", score_sum / game_count);
  string file;
  string fullPath;
  const char* filename;
  FILE* fp;
  int i;
  auto trun_itr = GameOver_list.begin();
  file = "state.txt";
  fullPath = dir + file;
  filename = fullPath.c_str();
  fp = fopen(filename, "w+");
  i = 0;
  trun_itr = GameOver_list.begin();
  for (auto itr = state_list.begin(); itr != state_list.end(); itr++) {
    i++;
    if ((trun_itr)->gameover_turn == i) {
      i = 0;
      fprintf(fp, "gameover_turn: %d; game: %d; progress: %d; score: %d\n",
              (trun_itr)->gameover_turn, (trun_itr)->game, (trun_itr)->progress,
              (trun_itr)->score);
      trun_itr++;
    } else {
      for (int j = 0; j < 9; j++) {
        fprintf(fp, "%d ", (*itr)[j]);
      }
      fprintf(fp, "\n");
    }
  }
  fclose(fp);
  file = "after-state.txt";
  fullPath = dir + file;
  filename = fullPath.c_str();
  fp = fopen(filename, "w+");
  i = 0;
  trun_itr = GameOver_list.begin();
  for (auto itr = after_state_list.begin(); itr != after_state_list.end();
       itr++) {
    i++;
    if ((trun_itr)->gameover_turn == i) {
      i = 0;
      fprintf(fp, "gameover_turn: %d; game: %d; progress: %d; score: %d\n",
              (trun_itr)->gameover_turn, (trun_itr)->game, (trun_itr)->progress,
              (trun_itr)->score);
      trun_itr++;
    } else {
      for (int j = 0; j < 9; j++) {
        fprintf(fp, "%d ", (*itr)[j]);
      }
      fprintf(fp, "\n");
    }
  }
  fclose(fp);
  file = "eval.txt";
  fullPath = dir + file;
  filename = fullPath.c_str();
  fp = fopen(filename, "w+");
  i = 0;
  trun_itr = GameOver_list.begin();
  for (auto itr = eval_list.begin(); itr != eval_list.end(); itr++) {
    i++;
    if ((trun_itr)->gameover_turn == i) {
      i = 0;
      fprintf(fp, "gameover_turn: %d; game: %d; progress: %d; score: %d\n",
              (trun_itr)->gameover_turn, (trun_itr)->game, (trun_itr)->progress,
              (trun_itr)->score);
      trun_itr++;
    } else {
      for (int j = 0; j < eval_length; j++) {
        if (j + 1 >= eval_length) {
          fprintf(fp, "%d ", (int)(*itr)[j]);
        } else {
          fprintf(fp, "%f ", (*itr)[j]);
        }
      }
      fprintf(fp, "\n");
    }
  }
  fclose(fp);
}