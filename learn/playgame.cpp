#include <cstdio>
#include <cstdlib>
#include <random>
#include <mutex>
#include <thread>
#include <vector>
using namespace std;
#include "game2048.h"
#include "symmetric.h"
#include "tdplayer2symmetric.h"
#include "util.h"

int putTile2Random(const board_t &board, mt19937 &mt);

#define NUM_GAMES 10000000
#define LOGCOUNT 10000
#define EVOUTPUT 2000000
#define NUM_THREADS 12
int loopCount = 0;

mutex mtx_for_loopcount;
mutex mtx_for_logger;

// logger
int logcount = 0;
long long sumS = 0; // ��ԍ��v
int maxS = 0; // ��ԍő�
int minS = 99999999; // ��ԍŏ�
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
      output_ev(logcount / EVOUTPUT);
    }
  }
  mtx_for_logger.unlock();
}
  

void run_tdlearning(int seed)
{
  // �����̏���
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
    while (true) { // turn ���Ƃ̃��[�v
      // CPU��: �����_��
      int play = putTile2Random(board, mt);
      board[play] = (rand(mt, 10) == 0) ? 2 : 1; // 4 �܂��� 2 ��u��
      
      // ����
      alldir_bool canMoves;
      alldir_board nextBoards;
      alldir_int scores;
      for (int d = 0; d < 4; d++) {
	scores[d] = moveB(board, nextBoards[d], (enum move_dir)d);
	canMoves[d] = (scores[d] > -1);
      }

      // �ȉ�, �����Ȃ��Ȃ�����while���𔲂��Ď��̃Q�[����
      if (!canMoves[0] && !canMoves[1] && !canMoves[2] && !canMoves[3]) {
	break;
      }

      // �v���C�������I��
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
  fprintf(stderr, "playgame seed");
}

int main(int argc, char** argv)
{
  if (argc < 1+1) {
    usage(); exit(1);
  }
  int seed = atoi(argv[1]);
    
  init_movetable();
  init_tuple(argv);
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

