#include <array>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <random>
#include <shared_mutex>
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
extern double expectimaxPlay(
    int depth,
    const board_t& board,
    unordered_map<unsigned long long, double>& ev_table);

#define NUM_THREADS 1

mutex mtx_for_logger;

#ifdef EXPECTIMAX_PARALLEL_ROOT
void canonicalizeBoard(const board_t& board, board_t& canonical)
{
  static constexpr int transforms[8][16] = {
      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
      { 3, 7,11,15, 2, 6,10,14, 1, 5, 9,13, 0, 4, 8,12},
      {15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
      {12, 8, 4, 0,13, 9, 5, 1,14,10, 6, 2,15,11, 7, 3},
      {12,13,14,15, 8, 9,10,11, 4, 5, 6, 7, 0, 1, 2, 3},
      {15,11, 7, 3,14,10, 6, 2,13, 9, 5, 1,12, 8, 4, 0},
      { 3, 2, 1, 0, 7, 6, 5, 4,11,10, 9, 8,15,14,13,12},
      { 0, 4, 8,12, 1, 5, 9,13, 2, 6,10,14, 3, 7,11,15}
  };

  for (int i = 0; i < 16; i++) canonical[i] = board[i];
  for (int transform = 1; transform < 8; transform++) {
    bool use_transform = false;
    for (int i = 0; i < 16; i++) {
      const int transformed = board[transforms[transform][i]];
      if (transformed < canonical[i]) {
        use_transform = true;
        break;
      }
      if (transformed > canonical[i]) break;
    }
    if (use_transform) {
      for (int i = 0; i < 16; i++) {
        canonical[i] = board[transforms[transform][i]];
      }
    }
  }
}

double expectimaxPlayParallel(
    int depth,
    const board_t& board,
    unordered_map<unsigned long long, double>& ev_table,
    shared_mutex& ev_table_mutex)
{
  board_t canonical_board;
  canonicalizeBoard(board, canonical_board);
  if (depth <= 1) return calcEvFiltered(canonical_board);

  extern unsigned long long hashvalue(int depth, const board_t& board);
  const unsigned long long hash = hashvalue(depth, canonical_board);
  {
    shared_lock<shared_mutex> lock(ev_table_mutex);
    const auto found = ev_table.find(hash);
    if (found != ev_table.end()) return found->second;
  }

  int empty_count = 0;
  double sum_score = 0;
  for (int index = 0; index < 16; index++) {
    if (canonical_board[index] != 0) continue;
    empty_count++;

    board_t put_board;
    for (int i = 0; i < 16; i++) put_board[i] = canonical_board[i];

    for (int tile = 1; tile <= 2; tile++) {
      put_board[index] = tile;
      double max_score = -DBL_MAX;
      for (int direction = 0; direction < 4; direction++) {
        board_t next_board;
        const int score = moveB(
            put_board, next_board, static_cast<enum move_dir>(direction));
        if (score == -1) continue;
        const double candidate = expectimaxPlayParallel(
            depth - 1, next_board, ev_table, ev_table_mutex) + score;
        if (candidate > max_score) max_score = candidate;
      }
      if (max_score == -DBL_MAX) max_score = 0;
      sum_score += max_score * (tile == 1 ? 9 : 1);
    }
  }

  const double value =
      empty_count == 0 ? 0 : sum_score / 10 / empty_count;
  {
    unique_lock<shared_mutex> lock(ev_table_mutex);
    return ev_table.emplace(hash, value).first->second;
  }
}

enum move_dir selectHandExpectimaxCanonicalSequential(
    const alldir_bool& can_moves,
    const alldir_board& next_boards,
    const alldir_int& scores,
    int depth,
    unordered_map<unsigned long long, double>& ev_table,
    shared_mutex& ev_table_mutex)
{
  array<double, 4> next_ev = {0, 0, 0, 0};
  int selected = -1;
  for (int direction = 0; direction < 4; direction++) {
    if (!can_moves[direction]) continue;
    next_ev[direction] = expectimaxPlayParallel(
        depth, next_boards[direction], ev_table, ev_table_mutex) +
        scores[direction];
    if (selected == -1 || next_ev[direction] > next_ev[selected]) {
      selected = direction;
    }
  }
  return static_cast<enum move_dir>(selected);
}

enum move_dir selectHandExpectimaxParallel(
    const alldir_bool& canMoves,
    const alldir_board& nextBoards,
    const alldir_int& scores,
    int depth,
    unordered_map<unsigned long long, double>& ev_table,
    shared_mutex& ev_table_mutex)
{
  array<double, 4> next_ev = {0, 0, 0, 0};
  vector<thread> workers;

  for (int direction = 0; direction < 4; direction++) {
    if (!canMoves[direction]) continue;

    workers.emplace_back([&, direction]() {
      next_ev[direction] =
          expectimaxPlayParallel(
              depth,
              nextBoards[direction],
              ev_table,
              ev_table_mutex) +
          scores[direction];
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  int selected = -1;
  for (int direction = 0; direction < 4; direction++) {
    if (!canMoves[direction]) continue;
    if (selected == -1 || next_ev[direction] > next_ev[selected]) {
      selected = direction;
    }
  }

  return static_cast<enum move_dir>(selected);
}
#endif

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
#ifdef EXPECTIMAX_PARALLEL_ROOT
  shared_mutex ev_table_mutex;
#endif

  const string state_path = output_dir + "/state.txt";
  const string after_state_path = output_dir + "/after_state.txt";
  const string eval_path = output_dir + "/eval.txt";

  FILE* state_fp = fopen(state_path.c_str(), "w");
  FILE* after_state_fp = fopen(after_state_path.c_str(), "w");
  FILE* eval_fp = fopen(eval_path.c_str(), "w");

  if (!state_fp || !after_state_fp || !eval_fp) {
    perror("fopen output");
    if (state_fp) fclose(state_fp);
    if (after_state_fp) fclose(after_state_fp);
    if (eval_fp) fclose(eval_fp);
    exit(EXIT_FAILURE);
  }

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

        const int gameover_turn = turn - 1;
        const int progress = progress_calculation(board);

        fprintf(state_fp,
                "gameover_turn:%d game:%d progress:%d score:%d\n",
                gameover_turn, game_seed, progress, myScore);
        fprintf(after_state_fp,
                "gameover_turn:%d game:%d progress:%d score:%d\n",
                gameover_turn, game_seed, progress, myScore);
        fprintf(eval_fp,
                "gameover_turn:%d game:%d progress:%d score:%d\n",
                gameover_turn, game_seed, progress, myScore);

        fflush(state_fp);
        fflush(after_state_fp);
        fflush(eval_fp);

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

      for (int i = 0; i < 16; i++) {
        fprintf(state_fp, "%d ", board[i]);
      }
      fprintf(state_fp, "\n");

      // =========================
      // select move
      // =========================

#ifdef EXPECTIMAX_PARALLEL_ROOT
      int dir;
      if (search_depth >= 3) {
        ev_table.clear();
#ifdef EXPECTIMAX_CANONICAL_SEQUENTIAL
        dir = selectHandExpectimaxCanonicalSequential(
            canMoves,
            nextBoards,
            scores,
            search_depth,
            ev_table,
            ev_table_mutex);
#else
        dir = selectHandExpectimaxParallel(
            canMoves,
            nextBoards,
            scores,
            search_depth,
            ev_table,
            ev_table_mutex);
#endif
      }
      else {
        ev_table.clear();
        dir = player.selectHandExpectimax(
            board,
            canMoves,
            nextBoards,
            scores,
            search_depth,
            ev_table);
      }
#else
      ev_table.clear();
      int dir =
          player.selectHandExpectimax(
              board,
              canMoves,
              nextBoards,
              scores,
              search_depth,
              ev_table);
#endif

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

      for (int i = 0; i < 16; i++) {
        fprintf(after_state_fp, "%d ", board[i]);
      }
      fprintf(after_state_fp, "\n");

      // =========================
      // save eval
      // =========================

      fprintf(eval_fp,
              "%f %f %f %f %d \n",
              (double)ev[0],
              (double)ev[1],
              (double)ev[2],
              (double)ev[3],
              progress_calculation(board));

      turn++;
    }
  }

  fclose(state_fp);
  fclose(after_state_fp);
  fclose(eval_fp);
}

void usage()
{
  fprintf(stderr,
          "test_expectimax start_seed game_counts depth evfile\n");
}

#ifndef EXPECTIMAX_NO_MAIN
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
#ifdef EXPECTIMAX_PARALLEL_ROOT
#ifdef EXPECTIMAX_CANONICAL_SEQUENTIAL
  base += "-canonical-sequential";
#else
  base += "-parallel";
#endif
#endif

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
#endif
