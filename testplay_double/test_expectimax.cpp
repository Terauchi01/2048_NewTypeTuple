#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <list>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

#include "game2048.h"
#include "symmetric.h"
#include "tdplayer_VSE_symmetric2.h"
#include "util.h"

extern int calcEvFiltered(const board_t &board);

#define NUM_THREADS 1

mutex mtx_for_logger;

class GameOver {
 public:
  int gameover_turn;
  int game;
  int progress;
  int score;

  GameOver(int gt, int g, int p, int s)
      : gameover_turn(gt),
        game(g),
        progress(p),
        score(s) {}
};

int progress_calculation(const board_t &board)
{
  int sum = 0;

  for (int i = 0; i < 16; i++) {
    if (board[i] != 0) {
      sum += (1 << board[i]);
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

  return availablePoints[rand(mt, count)];
}

void run_test_expectimax(int search_depth,
                         int seed_start,
                         int seed_end,
                         const string& output_dir)
{
  unordered_map<unsigned long long, double> ev_table;

  list<array<int, 16>> state_list;
  list<array<int, 16>> after_state_list;

  const int eval_length = 5;
  list<array<double, eval_length>> eval_list;

  list<GameOver> gameover_list;

  for (int game_seed = seed_start;
       game_seed < seed_end;
       game_seed++) {

    mt19937 mt(game_seed);

    TDPlayer player;

    board_t board;

    for (int i = 0; i < 16; i++) {
      board[i] = 0;
    }

    int play = putTile2Random(board, mt);

    if (play >= 0) {
      board[play] = (rand(mt, 10) == 0) ? 2 : 1;
    }

    int myScore = 0;
    int turn = 1;

    player.gameStart();

    while (true) {

      // =========================
      // random tile
      // =========================

      play = putTile2Random(board, mt);

      if (play < 0) {
        break;
      }

      board[play] = (rand(mt, 10) == 0) ? 2 : 1;

      // =========================
      // move generation
      // =========================

      alldir_bool canMoves;
      alldir_board nextBoards;
      alldir_int scores;

      for (int d = 0; d < 4; d++) {
        scores[d] =
            moveB(board,
                  nextBoards[d],
                  (enum move_dir)d);

        canMoves[d] = (scores[d] > -1);
      }

      // =========================
      // game over
      // =========================

      if (!canMoves[0] &&
          !canMoves[1] &&
          !canMoves[2] &&
          !canMoves[3]) {

        player.gameEnd();

        gameover_list.push_back(
            GameOver(turn-1,
                     game_seed,
                     progress_calculation(board),
                     myScore));

        mtx_for_logger.lock();

        {
          printf("game,%d,sco,%d,big,%d,turn,%d\n",
                 game_seed,
                 myScore,
                 biggestTile(board),
                 turn);

          fflush(stdout);
        }

        mtx_for_logger.unlock();

        break;
      }

#ifdef DEBUG_PLAY
      print_board(board);
#endif

      // =========================
      // calc ev
      // =========================

      int ev[4] = {0, 0, 0, 0};

      for (int d = 0; d < 4; d++) {

        if (!canMoves[d]) continue;

        ev[d] =
            calcEvFiltered(nextBoards[d]) +
            (scores[d] << 10);
      }

      // =========================
      // save state
      // =========================

      state_list.push_back({
          board[0],  board[1],  board[2],  board[3],
          board[4],  board[5],  board[6],  board[7],
          board[8],  board[9],  board[10], board[11],
          board[12], board[13], board[14], board[15]
      });

      // =========================
      // select move
      // =========================

      ev_table.clear();

      int dir =
          player.selectHandExpectimax(
              board,
              canMoves,
              nextBoards,
              scores,
              search_depth,
              ev_table);

      // =========================
      // apply move
      // =========================

      for (int i = 0; i < 16; i++) {
        board[i] = nextBoards[dir][i];
      }

      myScore += scores[dir];

      // =========================
      // save after-state
      // =========================

      after_state_list.push_back({
          board[0],  board[1],  board[2],  board[3],
          board[4],  board[5],  board[6],  board[7],
          board[8],  board[9],  board[10], board[11],
          board[12], board[13], board[14], board[15]
      });

      // =========================
      // save eval
      // =========================

      eval_list.push_back({
          (double)ev[0],
          (double)ev[1],
          (double)ev[2],
          (double)ev[3],
          (double)progress_calculation(board)
      });

      turn++;
    }
  }

  // =====================================================
  // write files
  // =====================================================


  // =====================================================
  // state.txt
  // =====================================================

{
    string state_path = output_dir + "/state.txt";
    FILE* fp = fopen(state_path.c_str(), "w");

    int i = 0;

    auto gitr = gameover_list.begin();

    for (auto itr = state_list.begin();
         itr != state_list.end();
         itr++) {

      // まず盤面を書く
      for (int j = 0; j < 16; j++) {
        fprintf(fp, "%d ", (*itr)[j]);
      }

      fprintf(fp, "\n");

      i++;

      // そのあと gameover 判定
      if (gitr != gameover_list.end() &&
          gitr->gameover_turn == i) {

        fprintf(fp,
                "gameover_turn:%d game:%d progress:%d score:%d\n",
                gitr->gameover_turn,
                gitr->game,
                gitr->progress,
                gitr->score);

        i = 0;
        gitr++;
      }
    }

    fclose(fp);
}

  // =====================================================
  // after_state.txt
  // =====================================================

{
    string after_state_path = output_dir + "/after_state.txt";
    FILE* fp = fopen(after_state_path.c_str(), "w");

    int i = 0;

    auto gitr = gameover_list.begin();

    for (auto itr = after_state_list.begin();
         itr != after_state_list.end();
         itr++) {

      for (int j = 0; j < 16; j++) {
        fprintf(fp, "%d ", (*itr)[j]);
      }

      fprintf(fp, "\n");

      i++;

      if (gitr != gameover_list.end() &&
          gitr->gameover_turn == i) {

        fprintf(fp,
                "gameover_turn:%d game:%d progress:%d score:%d\n",
                gitr->gameover_turn,
                gitr->game,
                gitr->progress,
                gitr->score);

        i = 0;
        gitr++;
      }
    }

    fclose(fp);
}

  // =====================================================
  // eval.txt
  // =====================================================

{
    string eval_path = output_dir + "/eval.txt";
    FILE* fp = fopen(eval_path.c_str(), "w");

    int i = 0;

    auto gitr = gameover_list.begin();

    for (auto itr = eval_list.begin();
         itr != eval_list.end();
         itr++) {

      for (int j = 0; j < eval_length; j++) {

        if (j == eval_length - 1) {
          fprintf(fp, "%d ", (int)(*itr)[j]);
        }
        else {
          fprintf(fp, "%f ", (*itr)[j]);
        }
      }

      fprintf(fp, "\n");

      i++;

      if (gitr != gameover_list.end() &&
          gitr->gameover_turn == i) {

        fprintf(fp,
                "gameover_turn:%d game:%d progress:%d score:%d\n",
                gitr->gameover_turn,
                gitr->game,
                gitr->progress,
                gitr->score);

        i = 0;
        gitr++;
      }
    }

    fclose(fp);
}
}

void usage()
{
  fprintf(stderr,
          "test_expectimax start_seed game_counts depth evfile\n");
}

int main(int argc, char** argv)
{
  if (argc != 5) {
    usage();
    exit(1);
  }

  int seed = atoi(argv[1]);
  int counts = atoi(argv[2]);
  int depth = atoi(argv[3]);

  char* filename = argv[4];

  init_movetable();

  input_ev(filename);

  vector<thread> ths;

  string base = fs::path(filename).stem().string()+to_string(depth);

  string dirname = "board_data/" + base;

  fs::create_directories(dirname);

  for (int i = 0; i < NUM_THREADS; i++) {

    ths.push_back(
        thread(run_test_expectimax,
              depth,
              seed + i * counts / NUM_THREADS,
              seed + (i + 1) * counts / NUM_THREADS,
              dirname));
  }

  for (auto& th : ths) {
    th.join();
  }

  return 0;
}