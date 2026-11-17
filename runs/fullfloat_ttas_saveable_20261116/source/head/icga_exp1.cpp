#include <cstdio>
#include <cstdlib>
#include <random>
#include <mutex>
#include <thread>
#include <vector>
using namespace std;
#include "game2048.h"
#include "symmetric.h"
#include "tc_backward_symmetric_player_s.h"
#include "gameresult_logger.h"
#include "util.h"

#define NUM_UPDATES 10000000000LL
#define LEARNLOOP 1
#define NUM_LASTPLAY 500
#define NUM_EXPECTIMAXPLAY 500
#define NUM_THREADS 12

// #define NUM_TURN_INIT 10
#define NUM_TURN_INIT 10000000

globalCounter glCounter_play;
globalCounter glCounter_actions;

void learn_play(int learnLoop, int seed)
{
  // 乱数の準備
  mt19937 mt(10000 * learnLoop + seed);

  TDPlayer player;
  gameInfo game;
  int start_turn = 0;
  int end_turn = 0;
  while (true) {
    // ゲームの初期化
    if (end_turn - start_turn < NUM_TURN_INIT) {
      start_turn = 0;
      player.gameStart();
      player.learn_start = 0;
      game.gameStart(glCounter_play.increment());
      putTile2Random(game.board, mt);
    } else {
      // copy status from middle of history
      start_turn = (start_turn + end_turn) / 2;
      player.turn = start_turn;
      player.learn_start = player.turn;
      game.gameStart(glCounter_play.increment());
      game.turn = player.turn + 1;
      for (int i = 0; i < player.turn; i++) game.myScore += player.sc_history[i];
      copyBoard(player.bd_history[player.turn - 1], game.board);
    }

    while (true) { // turn ごとのループ
      bool isGameEnd = game.processBeforeSelect(mt);
      if (isGameEnd) break;

      // プレイヤが手を選ぶ
      int dir = player.selectHand(game.board, game.canMoves, game.nextBoards, game.scores);

      game.processAfterSelect(dir);
    }
    player.gameEnd();
    game.gameEnd();
    end_turn = game.turn;
    gameresult_logger::output(game.curLoop, game.myScore, game.board, start_turn, end_turn);

    long long actions = glCounter_actions.increment(end_turn - start_turn);
    if ( actions >= NUM_UPDATES * (learnLoop + 1)) {
      break;
    }
  }
}

globalCounter glCounter_lastplay;
void greedy_play(int learnLoop, int seed)
{
  // 乱数の準備
  mt19937 mt(10000 * learnLoop + seed);

  TDPlayer player; player.learningActive = false;
  gameInfo game;
  while (true) {
    int num_play = glCounter_lastplay.increment();
    if (num_play > NUM_LASTPLAY) break;
    player.gameStart();
    game.gameStart(num_play);

    putTile2Random(game.board, mt);
    while (true) { // turn ごとのループ
      bool isGameEnd = game.processBeforeSelect(mt);
      if (isGameEnd) break;

      // プレイヤが手を選ぶ
      int dir = player.selectHand(game.board, game.canMoves, game.nextBoards, game.scores);

      game.processAfterSelect(dir);
    }
    player.gameEnd();
    game.gameEnd();
    gameresult_logger::output(game.curLoop, game.myScore, game.board, 0, game.turn);
  }
}

globalCounter glCounter_lastexpectimax;
void expectimax_play(int learnLoop, int seed)
{
  // 乱数の準備
  mt19937 mt(10000 * learnLoop + seed);

  TDPlayer player; player.learningActive = false;
  gameInfo game;
  while (true) {
    int num_play = glCounter_lastexpectimax.increment();
    if (num_play > NUM_EXPECTIMAXPLAY) break;
    player.gameStart();
    game.gameStart(num_play);

    putTile2Random(game.board, mt);
    while (true) { // turn ごとのループ
      bool isGameEnd = game.processBeforeSelect(mt);
      if (isGameEnd) break;

      // プレイヤが手を選ぶ
      int dir = player.selectHandExpectimax(game.board, game.canMoves, game.nextBoards, game.scores, 3);

      game.processAfterSelect(dir);
    }
    player.gameEnd();
    game.gameEnd();
    gameresult_logger::output(game.curLoop, game.myScore, game.board, 0, game.turn);
  }
}

void usage(char** argv) {
  fprintf(stderr, "%s <seed> <tuples>\n", argv[0]);
}

int main(int argc, char** argv)
{
  if (argc < 1+1+NUM_TUPLE) {
    usage(argv); exit(1);
  }
  int seed = atoi(argv[1]);

  int selects[NUM_TUPLE];
  for (int i = 0; i < NUM_TUPLE; i++) {
    selects[i] = atoi(argv[2+i]);
  }
  select_tuple(selects);

  init_movetable();
  init_tuple();
  init_ev();
  printf("initialization finished\n");
  gameresult_logger::init();

  long long learningtime = 0;
  for (int learnLoop = 0; learnLoop < LEARNLOOP; learnLoop++) {
    stopwatch sw;
    glCounter_play.reset();
    LOGCOUNT = 1<<30; // no statistic output
    std::vector<std::thread> ths;
    for (int i = 0; i < NUM_THREADS; i++) {
      ths.push_back(std::thread(learn_play, learnLoop, seed+i));
    }
    for (auto& th : ths) th.join(); ths.clear();
    learningtime += sw.millis();
    printf("finished Learning\n");

    glCounter_lastplay.reset();
    gameresult_logger::reset();
    LOGCOUNT = NUM_LASTPLAY;
    for (int i = 0; i < NUM_THREADS; i++) {
      ths.push_back(std::thread(greedy_play, learnLoop, seed+i));
    }
    for (auto& th : ths) th.join(); ths.clear();
    printf("GreedyPlay End\n");
    
    glCounter_lastexpectimax.reset();
    gameresult_logger::reset();
    LOGCOUNT = NUM_EXPECTIMAXPLAY;
    for (int i = 0; i < NUM_THREADS; i++) {
      ths.push_back(std::thread(expectimax_play, learnLoop, seed+i));
    }
    for (auto& th : ths) th.join(); ths.clear();
    printf("ExpectimaxPlay End\n");
    printf("Total Learning Time: %8.5f\n", (learningtime / 1000) / 3600.0);
  }
  return 0;
}
