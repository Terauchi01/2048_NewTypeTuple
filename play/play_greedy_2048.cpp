// g++ Play_perfect_player.cevals -std=c++20 -mcmodel=large -O2
#include <array>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
int putTile2Random(const board_t &board, mt19937 &mt)
{
  int availablePoints[16];
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (board[i] != 0) continue;
    availablePoints[count++] = i;
  }
  if (count == 0) return -1;
  std::uniform_int_distribution<int> dist(0, count - 1);
  return availablePoints[dist(mt)];
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
  char name[512];
  // basename を取得
  const char* last_slash = strrchr(filename, '/');
  const char* base = last_slash ? last_slash + 1 : filename;
  strncpy(name, base, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';

  // 拡張子を削除 (.zip, .dat など)
  char* ext = strstr(name, ".zip"); if (ext) *ext = '\0';
  ext = strstr(name, ".dat"); if (ext) *ext = '\0';

  // キーワードに続く数字を探して設定する
  char* p;
  p = strstr(name, "tuples");
  if (p) params.NT = atoi(p + strlen("tuples"));
  p = strstr(name, "NUM_TUPLE");
  if (p) params.tupleNumber = atoi(p + strlen("NUM_TUPLE"));
  p = strstr(name, "TupleNumber");
  if (p) params.tupleNumber = atoi(p + strlen("TupleNumber"));
  p = strstr(name, "Multistaging");
  if (p) params.multiStaging = atoi(p + strlen("Multistaging"));
  p = strstr(name, "OI");
  if (p) params.oi = atoi(p + strlen("OI"));
  p = strstr(name, "seed");
  if (p) params.seed = atoi(p + strlen("seed"));
  p = strstr(name, "count");
  if (p) params.c = atoi(p + strlen("count"));
  // 旧形式で単独の 'c' が使われることがあれば検出
  if (params.c == 0) {
    p = strstr(name, "-c");
    if (p) params.c = atoi(p + 2);
  }
  p = strstr(name, "mini");
  if (p) params.mini = atoi(p + strlen("mini"));

  // 予備: もし先頭が数字だけの形式なら NT とみなす
  if (params.NT == 0) {
    if (isdigit((unsigned char)name[0])) params.NT = atoi(name);
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

  // Initialize movement lookup tables required by moveB()
  init_movetable();

  // FILE* fp = fopen(evfile, "rb");
  // srand(seed);
  // Use mt19937 seeded with given seed and helper lambdas for randomness
  std::mt19937 mt(seed);
  auto rnd_index = [&](int n) -> int { std::uniform_int_distribution<int> d(0, n - 1); return d(mt); };
  auto rnd_chance = [&](int mod) -> int { std::uniform_int_distribution<int> d(0, mod - 1); return d(mt); };
  list<array<int, 9>> state_list;
  list<array<int, 9>> after_state_list;
  const int eval_length = 5;
  list<array<double, eval_length>> eval_list;
  list<GameOver> GameOver_list;
  // Simple greedy play using TDPlayer and game2048 core functions
  printf("Starting game simulation with seed: %d\n", seed);
  for (int gid = 1; gid <= game_count; gid++) {
    board_t board;
    for (int i = 0; i < 16; i++) board[i] = 0;
    int play = putTile2Random(board, mt);
    if (play >= 0) board[play] = (rnd_chance(10) == 0) ? 2 : 1; // 4 または 2 を配置
    // play = putTile2Random(board, mt);
    // if (play >= 0 && board[play] == 0) board[play] = (rnd_chance(10) == 0) ? 2 : 1; // 4 または 2 を配置
    // else if (play >= 0 && board[play] != 0) board[(play+1)%16] = (rnd_chance(10) == 0) ? 2 : 1; // 4 または 2 を配置
    printf("Starting game %d\n", gid);
    int turn = 0;
    int game_score = 0;

    // place two initial tiles (like game start)
    for (int k = 0; k < 2; k++) {
      int p = -1;
      int empty[16]; int ec = 0;
      for (int i = 0; i < 16; i++) if (board[i] == 0) empty[ec++] = i;
      if (ec > 0) p = empty[rnd_index(ec)];
      if (p >= 0) board[p] = (rnd_chance(10) == 0) ? 2 : 1;
    }

    while (true) {
      int play = putTile2Random(board, mt);
      if (play >= 0) board[play] = (rnd_chance(10) == 0) ? 2 : 1; // 4 または 2 を配置
      turn++;
      // prepare nextBoards and scores
      alldir_bool canMoves;
      alldir_board nextBoards;
      alldir_int scores;
      for(int i = 0;i < 16 ;i++){
        printf(" %d", board[i]);
        if(i % 4 == 3)printf("\n");
      }
      for (int d = 0; d < 4; d++) {
        scores[d] = moveB(board, nextBoards[d], (enum move_dir)d);
        canMoves[d] = (scores[d] > -1);
        // printf("score: %d\n", scores[d]);
      }

      // pick best move
      int ev[4] = {0,0,0,0};
      int selected = -1; int bestv = INT_MIN;
      for (int d = 0; d < 4; d++) {
        if (!canMoves[d]) continue;
        ev[d] = calcEvFiltered(nextBoards[d]) + (scores[d] << 10);
        if (selected == -1 || ev[d] > bestv) { selected = d; bestv = ev[d]; }
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
      eval_list.push_back(array<double, eval_length>{(double)ev[0],ev[1],ev[2],ev[3],(double)progress_calculation(board)});

      // add random tile
      int empty[16]; int ec = 0;
      for (int i = 0; i < 16; i++) if (board[i] == 0) empty[ec++] = i;
      if (ec > 0) {
        int p = empty[rnd_index(ec)];
        board[p] = (rnd_chance(10) == 0) ? 2 : 1;
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