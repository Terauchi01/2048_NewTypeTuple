#include <cstdio>
#include <cstdlib>
#include <random>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <ctime>
using namespace std;
#include "../head/game2048.h"
#include "../head/symmetric.h"
#include "../head/tdplayer_VSE_symmetric2.h"
#include "../head/util.h"

int putTile2Random(const board_t &board, mt19937 &mt);
// #define NUM_GAMES 100000000
// #define NUM_GAMES 10
#define NUM_STEPS 200000000000
// #define NUM_STEPS 200
#define LOGCOUNT 10000
#define EVOUTPUT 10000000000
#define NUM_THREADS 3
#define RESTART_LENGTH 128
int loopCount = 0;
int seed = 0;
long long stepCount = 0;
bool save_flg = 0;

mutex mtx_for_loopcount;
mutex mtx_for_logger;

// ログ開始時刻
std::chrono::steady_clock::time_point start_time;

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
      // 10000区切りのときに現在時刻と経過時間、累積ターン数、平均ターン/秒を出力
      if(logcount % 10000 == 0 || logcount == 1){
        // システム時刻（人間可読）
        auto now_sys = std::chrono::system_clock::now();
        time_t tt = std::chrono::system_clock::to_time_t(now_sys);
        char timestr[64];
        struct tm tminfo = *localtime(&tt);
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tminfo);

        // 経過秒数と平均ターン/秒を計算
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count();
        double steps_per_sec = (elapsed > 0) ? (double)stepCount / (double)elapsed : 0.0;

        printf("time,%s,elapsed_sec,%lld,steps,%lld,steps_per_sec,%.2f\n",
               timestr, (long long)elapsed, (long long)stepCount, steps_per_sec);
      }

      printf("statistics,%d,ave,%d,max,%d,min,%d\n", logcount, (int)(sumS / LOGCOUNT), maxS, minS);
      sumS = 0;
      maxS = 0;
      minS = 99999999;
    }
    if (save_flg) {
      output_ev(seed, stepCount / EVOUTPUT);
      save_flg = 0;
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
      // 終了条件はゲーム数ではなくゲーム内のターン数（stepCount）にする
      if (stepCount >= NUM_STEPS) { mtx_for_loopcount.unlock(); break; }
      curLoop = loopCount++; // ゲーム番号（表示用）
    }
    mtx_for_loopcount.unlock();

    board_t board;
    for (int i = 0; i < 16; i++) board[i] = 0;
    int myScore = 0;
    int turn = 1;
    int restart_points_board[100000][16];
    int restart_scores[100000];
    int restart_count = 0;
    int restart_start = 0;
    int restart_turns[1000] = {0}; // リスタートしたターン数を記録する配列
    int restart_turns_end[1000];
    
    player.gameStart();
    int save_loop;
    int save_score;
    int save_bigtile;
    int save_turn;
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
          player.gameEnd();
          if(restart_count == 0){
            //後で保存するようのリスタートしてない時の変数の保存
            save_loop = curLoop+1;
            save_score = myScore;
            save_bigtile = biggestTile(board);
            save_turn = turn;
          }
	  restart_turns_end[restart_count++] = turn; // リスタートしたターン数を記録
          // ターン（学習回数）をカウント
          mtx_for_loopcount.lock();
          {
            long long prev_stepCount = stepCount;
            stepCount += turn-restart_start;
            // EVOUTPUT間隔で保存フラグを設定（境界をまたいだ場合）
            if ((prev_stepCount / EVOUTPUT) < (stepCount / EVOUTPUT)) {
              save_flg = 1;
            }
          }
          mtx_for_loopcount.unlock();
	  if (turn - restart_start < RESTART_LENGTH) {
            // リスタート不可：進捗が足りない
            // printf("no restart turn %d\n", turn - restart_start);
            break;
          } else {
            // リスタート実行
            int new_restart_point = (restart_start + turn) / 2;
            restart_turns[restart_count] = new_restart_point; // 現在のターン数を記録

            // 状態を過去のポイントに復元
            restart_start = new_restart_point;
            turn = restart_start;
            
            // ボード状態を復元
            for (int i = 0; i < 16; i++) {
              board[i] = restart_points_board[restart_start][i];
            }
            // スコアを復元
            myScore = restart_scores[restart_start];
            
            // 復元したボードから次の手を計算
            for (int d = 0; d < 4; d++) {
                scores[d] = moveB(board, nextBoards[d], (enum move_dir)d);
                canMoves[d] = (scores[d] > -1);
            }
            
            // printf("restart_executed: turn=%d, score=%d, big=%d\n", turn, myScore, biggestTile(board));
            // リスタート後は通常フローに戻る（break しない）
          }
      }

      // プレイヤーの手選択
      int dir = player.selectHand(board, canMoves, nextBoards, scores);
      for (int i = 0; i < 16; i++) {
          int tmp = nextBoards[dir][i];
          board[i] = tmp;
          restart_points_board[turn][i] = tmp; 
      }
      myScore += scores[dir];
      restart_scores[turn] = myScore;
      turn++;
    }

    mtx_for_loopcount.lock();
    {
    // printf("game,%d,sco,%d,big,%d,turn,%d\n", curLoop+1, myScore, biggestTile(board), turn);
    // リスタート履歴をまとめて出力
      // 初回のログ
      printf("game,%d,sco,%d,big,%d,turn,%d\n", save_loop, save_score, save_bigtile, save_turn);
      printf("restarts,thread,%d,turns", seed % NUM_THREADS);
      for (int i = 0; i < restart_count; i++) {
        printf(",%d", restart_turns_end[i]);
      }
      printf("\n");
    }
    mtx_for_loopcount.unlock();
    
    logger(save_score);
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

  // ログ開始時刻を記録
  start_time = std::chrono::steady_clock::now();
    
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
