#include <cstdio>
#include <cstdlib>
#include <random>
#include <mutex>
#include <thread>
#include <vector>
using namespace std;
#include "../head/game2048.h"
#include "../head/symmetric.h"
#include "../head/tdplayer_VSE_symmetric.h"
#include "../head/util.h"

int putTile2Random(const board_t &board, mt19937 &mt);
#define NUM_GAMES 100000000
// #define NUM_GAMES 10
#define LOGCOUNT 10000
#define EVOUTPUT 1000000
#define NUM_THREADS 1
int loopCount = 0;
int seed = 0;

mutex mtx_for_loopcount;
mutex mtx_for_logger;

// logger
int logcount = 0;
long long sumS = 0; // スコア合計
int maxS = 0; // スコア最大
int minS = 99999999; // スコア最小
inline void logger(int score)
{
  mtx_for_logger.lock();
  {
    logcount++;
    sumS += score;
    if (maxS < score) maxS = score;
    if (minS > score) minS = score;

    if (logcount % LOGCOUNT == 0) {
      printf("statistics,%d,ave,%d,max,%d,min,%d\n", logcount, (int)(sumS / LOGCOUNT), maxS, minS);
      sumS = 0;
      maxS = 0;
      minS = 99999999;
    }
    if (logcount % EVOUTPUT == 0) {
      output_ev(seed, logcount / EVOUTPUT);
    }
  }
  mtx_for_logger.unlock();
}
  

void run_tdlearning(int seed)
{
  // 乱数の初期化
  mt19937 mt(seed);
  
  TDPlayer player;
  while (true) {
    int curLoop;
    mtx_for_loopcount.lock();
    {
      if (loopCount >= NUM_GAMES) { mtx_for_loopcount.unlock(); break; }
      curLoop = loopCount++;
    }
    mtx_for_loopcount.unlock();

    board_t board;
    for (int i = 0; i < 16; i++) board[i] = 0;
    int myScore = 0;
    int turn = 1;
    
    player.gameStart();
    while (true) { // turn ループのターンループ
      // CPU側: ランダム配置
      int play = putTile2Random(board, mt);
      board[play] = (rand(mt, 10) == 0) ? 2 : 1; // 4 または 2 を配置
      
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
	break;
      }

      // プレイヤーの手選択
      int dir = player.selectHand(board, canMoves, nextBoards, scores);
      for (int i = 0; i < 16; i++) {
	board[i] = nextBoards[dir][i];
      }
      myScore += scores[dir];
      turn++;
    }
    player.gameEnd();

    printf("game,%d,sco,%d,big,%d,turn,%d\n", curLoop+1, myScore, biggestTile(board), turn);
    logger(myScore);
  }
}

void usage() {
  fprintf(stderr, "playgame_VSE seed\n");
}

int main(int argc, char** argv)
{
  if (argc != 2) {  // プログラム名 + seed の2つの引数のみ
    usage(); exit(1);
  }
  seed = atoi(argv[1]);
    
  init_movetable();
  init_tuple();  // 全てのタプルを使用するため引数不要
  printf("initialization finished\n");

  std::vector<std::thread> ths;
  for (int i = 0; i < NUM_THREADS; i++) {
    ths.push_back(std::thread(run_tdlearning, seed+i));
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
