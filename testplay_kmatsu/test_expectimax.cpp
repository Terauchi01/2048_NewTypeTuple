#include <cstdio>
#include <cstdlib>
#include <random>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
using namespace std;
#include "../head/game2048.h"
#include "../head/symmetric.h"
#include "../head/tdplayer_VSE_symmetric2.h"
#include "../head/util.h"

int putTile2Random(const board_t &board, mt19937 &mt);
#define NUM_THREADS 5
int seed = 0;
mutex mtx_for_logger;

void run_test_expectimax(int search_depth, int seed_start, int seed_end)
{
  unordered_map<unsigned long long, int> ev_table;
  for (int game_seed = seed_start; game_seed < seed_end; game_seed++) {
    // 乱数の初期化
    mt19937 mt(game_seed);
    TDPlayer player;

    board_t board; for (int i = 0; i < 16; i++) board[i] = 0;
    int play = putTile2Random(board, mt);
    board[play] = (rand(mt, 10) == 0) ? 2 : 1; // 4 または 2 を配置
    int myScore = 0;
    int turn = 1;

    player.gameStart();
    while (true) { // turn ループのターンループ
      // CPU側: ランダム配置
      int play = putTile2Random(board, mt);
      board[play] = (rand(mt, 10) == 0) ? 2 : 1; // 4 または 2 を配置
      
      // printBoard(board);
      // 移動
      alldir_bool canMoves;
      alldir_board nextBoards;
      alldir_int scores;
      for (int d = 0; d < 4; d++) {
          scores[d] = moveB(board, nextBoards[d], (enum move_dir)d);
          canMoves[d] = (scores[d] > -1);
      }

      // 以下, 移動できないならwhile文を抜けて次のゲームへ
      if (!canMoves[0] && !canMoves[1] && !canMoves[2] && !canMoves[3]) {
          player.gameEnd();
	  mtx_for_logger.lock();
	  {
	    printf("game,%d,sco,%d,big,%d,turn,%d\n", game_seed, myScore, biggestTile(board), turn);
	    fflush(stdout);
	  }
          mtx_for_logger.unlock();
	  break;
      }

    #ifdef DEBUG_PLAY
      print_board(board);
    #endif
      // プレイヤーの手選択
      int dir = player.selectHandExpectimax(board, canMoves, nextBoards, scores, search_depth, ev_table);
      for (int i = 0; i < 16; i++) {
          board[i] = nextBoards[dir][i];
      }
      myScore += scores[dir];
      turn++;
    }
  }
}

void usage() {
  fprintf(stderr, "test_expectimax start_seed game_counts depth evfile\n");
}

int main(int argc, char** argv)
{
  if (argc != 1+4) {  // プログラム名 + seed の2つの引数のみ
    usage(); exit(1);
  }
  int seed = atoi(argv[1]);
  int counts = atoi(argv[2]);
  int depth = atoi(argv[3]);
  char* filename = argv[4];

  init_movetable();
  input_ev(filename);

  std::vector<std::thread> ths;
  for (int i = 0; i < NUM_THREADS; i++) {
    ths.push_back(std::thread(run_test_expectimax,
			      depth,
			      seed + i * counts / NUM_THREADS,
			      seed + (i+1)*counts / NUM_THREADS));
  }
  for (auto& th : ths) th.join();
}

int putTile2Random(const board_t &board, mt19937 &mt)
{
  int availablePoints[16];
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (board[i] != 0) continue;
    availablePoints[count++] = i;
  }
  return availablePoints[rand(mt, count)];
}
