#include <cstdio>
#include <random>
#include <cstring>
using namespace std;
#include "../head/game2048.h"
#include "../head/symmetric.h"
#include "../head/tdplayer_VSE_symmetric2.h"
#include "../head/util.h"
#include "../head/fixed_q10.hpp"
#include <random>
using namespace std;

#define NUM_STAGES 2
#define EV_INIT 320000
#define SIFT 10

// ステージ判定用閾値（この値以上の数字があるかでステージを決定）
#define STAGE_THRESHOLD 14

// デバッグフラグ (1にするとデバッグ情報を表示)
#define DEBUG_FILTERED_BOARDS 0

// calcEv デバッグ出力 (1 にするとログを出す)
#ifndef DEBUG_CALC_EV
#define DEBUG_CALC_EV 0
#endif

#ifndef TUPLE_FILE_TYPE
#define TUPLE_FILE_TYPE 6
#endif

#define MAX_TILE_VALUE 17

#if TUPLE_FILE_TYPE == 0
  #include "../head/selected_6G_tuples_sym.h"
  static const uint8_t filter_mapping[1][18] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 }
  };
  // #define DEFAULT_UNROLL_COUNT 72
#elif TUPLE_FILE_TYPE == 6
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
  #error "Invalid TUPLE_FILE_TYPE specified"
  static const uint8_t filter_mapping[4][18] = {
    { 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6 },
    { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5 }
  };
#endif

// --- _Pragma ラッパ ---
// 文字列化
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#if defined(__CUDACC__)
  #define UNROLL_PRAGMA(N) _Pragma(STR(unroll N))                     // NVCC
#elif defined(__clang__)
  #define UNROLL_PRAGMA(N) _Pragma(STR(clang loop unroll_count(N)))   // Clang / AppleClang
#elif defined(__GNUC__)
  #define UNROLL_PRAGMA(N) _Pragma(STR(GCC unroll N))                 // GCC
#else
  #define UNROLL_PRAGMA(N) /* fallback: 何もしない */
#endif

// apply mapping to produce NUM_SPLIT filtered boards
static inline void apply_filters_from_mapping(const board_t &board, board_t filtered_boards[NUM_SPLIT]) {
  UNROLL_PRAGMA(NUM_SPLIT*16)
  for (int fi = 0; fi < NUM_SPLIT*16; ++fi) {
    int f = fi/16;
    int i = fi%16; 
    int v = board[i];
    filtered_boards[f][i] = filter_mapping[f][v];
  }
}

int Evs[NUM_STAGES][NUM_SPLIT][NUM_TUPLE][ARRAY_LENGTH];
int pos[NUM_TUPLE][TUPLE_SIZE];

void input_ev(const char* filename) {
  FILE *fp;

  fp = fopen(filename, "rb");
  if (!fp) { perror("fopen"); return; }

  // タプルごとに ARRAY_LENGTH 個を読み込む
  // 注意: ARRAY_LENGTH が大きければ動的確保にする
  int *buf = (int*)malloc(sizeof(int) * ARRAY_LENGTH);
  if (!buf) { fclose(fp); return; }

  for (int s = 0; s < NUM_STAGES; s++) {
    for (int f = 0; f < NUM_SPLIT; f++) {
      for (int t = 0; t < NUM_TUPLE; t++) {
	size_t read = fread(Evs[s][f][t], sizeof(int), ARRAY_LENGTH, fp);
        if (read != (size_t)ARRAY_LENGTH) {
          perror("fread");
          free(buf);
          fclose(fp);
          return;
        }
      }
    }
  }

  free(buf);
  fclose(fp);
}

unsigned long long key_depth[10] = {5483587768483500242, 
  3289756756612544250,
  7383410230321913203,
  4389257939641668376,
  1648765404322538793,
  5477155372566008779,
  7774640943048400337,
  8818528432763958736,
  6630920841772088978,
  2547650555966819817,
};

unsigned long long key_tile[16][18] = {
{4791143743490746871,170063063709631371,8538736935661868719,1173445608790362334,6586052238769084556,1841003043470199778,4497934684336893719,2703034373355134233,97087649026585318,4859813450684958304,1936985247664257697,4614696968340629014,3187666622663826899,4758965596370721361,8488146225034819866,8472757910406613938,6446445964000557855,7178183125019924661,},
{7829946863872510717,4863011485231955191,3967702219641512610,4984059580254610527,1754991950772357438,7105995168942122800,6241837496066099599,6512234422804910257,4796361041909990686,58681791911672209,129425078834008153,1196492644688631469,671577075844565205,5766116694901824872,1254681150390615097,7850636598753419311,1255768678977997162,587442441069737332,},
{7608334776259630824,4212913842010010665,7982686209625519669,2719159064856825938,3560712509190177333,1178493009132394624,6470132608830878779,3903379553827924473,5581968058834174411,113043749200414413,2465549405874621463,1555568231093567487,8731027731087543341,137734980357872187,7731579932683569767,8020157059549835166,7795172666204287128,7008425346873945969,},
{5514778380350326426,6324700878037254929,3817875770248450916,4509781157369824932,1737412914794607176,4183443017638250187,2628019577932635074,2706357999956181689,2684643203378898623,2513106675872484626,4865784180242455077,6125330346557908714,1056550942026929588,2037676266559588444,3451052632928279279,1646234128876993206,3765530596817464708,7730032118075841497,},
{7146605091072587032,4177008515751983622,1490599153275916571,3135324879232244271,4053876835465103619,194996611813971574,7768131966713267259,1689765120251930929,2187829214390227201,5545878294454958820,1326902380616131393,8440229963543634741,8135031561035968280,325186699855397123,2834492042335956480,869363759490534105,4310937099421095416,6337792549945075627,},
{5833177496812603173,4497739184322064014,4977059420227903015,6626009795652998664,6397028326980672122,4377900333331136936,633982929289603425,7607858779572374185,616694517443756010,461513486572563123,5541442802298169478,2201234508706646493,7793588646799883647,6908273316260427336,2283083833913284240,8344893990963274970,1193706940783670674,3965714485011941191,},
{5981079868796271088,8296271395831510479,4412768124738105192,3986737429986712759,3248060593788038119,8269883537791922526,5439495608528221399,8993145068649528738,1652288541065655983,7811547015144581677,6086005121882156841,4444388390714544417,3479185682855506,4529376963455808701,1532629542876833297,9149976194029786016,3355935973227858540,4273641899574271489,},
{4607748581299767555,6845802127012572853,2295053831199100734,1285259763737734770,8919306485411230546,5550881091705388922,3261782826200102575,687263378995695734,4987254951696314583,8834359321701529435,624058632047225657,3797473242036968702,1972953512020503363,479722351737662237,6745379322470472023,3225715240582299545,4848324597727894976,855341147998083258,},
{3480087115188118094,8350399517746938329,8115828790748332148,1096738398123928698,4684543384415017382,3703814989375387793,6531671448586946965,6847585852389824455,7339678820502747098,9203027825495785966,4311958882569790733,3300343758656739455,6812671126105412607,5030584620892614169,6419815357977009885,4092226599592049789,934932146348589178,567833080208178801,},
{632357535994913162,1155707070334513006,665112421845549911,7462916453267836330,230592839181533615,6318403219239735370,3866198754942988216,7133814901845813220,1930223882885606020,6369588470749226566,8358831954311498781,3698752491876628904,1879533821465205430,3143394221030199602,2969195462452885425,2690507561489347425,3512676755798420118,2551177528245606099,},
{7518616682200292404,8637058554316664912,2637758510824086065,3126505039531437238,1434784021154887143,1750824357564916923,1718703298900238158,6612486110318210272,7475442733419782512,1648727573796932040,3584941280835221459,1312286009583526577,5745211837636834705,7427137538722069709,5092455666032190340,1375509090048841496,4296600851616251315,8438354710170059302,},
{7555390278710027007,928122085116682520,9206081736782488298,2194527748364919866,9185662102453362480,8973543649331804895,6507422772579597811,5686682353444910543,7021708928385502489,7746564745376001522,583939643686166389,2180790068672023715,1600730605890263640,7476031789885510714,4508225420600448915,2302960460327370852,5046864830651430971,62417356200873525,},
{9199097804558743743,2999870824010996442,1312668569106599837,6625536432365907044,8894031285126549225,3905744304658214767,1213901920798942555,4126777622226035212,6875480589216283248,507156475343539371,4855465025206665309,5041033143370401299,92361387409460027,8347240768876272830,9118622599268804031,938080548710234665,4943538338419336074,3012959909867485710,},
{3428260992723795266,1358971267057042899,262270328629425936,7547006791899303777,5433728456123885747,4994348662065936666,7747530229500756671,3867102766799457791,5086510148745744929,388714533712027953,6127904197258060376,6671911987733320979,3487347025646045735,6871880253185645754,407036392136258324,693969626693743679,8116100492528420312,7183647908387976718,},
{2606224607296594406,8669883822372153850,5102382456703365904,3145229150440737702,8383371849115453185,136383256085001693,8922933456445712861,5896403807330309774,2397605753035847648,6520241161600311232,537864307728543677,4786684454069906466,8987920331263334551,1987307619840840132,1011771247807598629,7764040464315506213,9050315623720570454,6474151242556319237,},
{8500638053127146340,3976003370156613427,7748022575956593610,1042270659454221903,7941478879585315306,1946941771994464479,7056892885998025527,3991794477539050445,6351287056175000513,4323747652998734246,3852645767808253643,52436639027148063,478166626033810462,8028966089324140174,5294518532944456965,506508722678850307,3309346273279857152,544655186961705025,},
  };

const int symmetric[8][16] =
  {{ 0, 1, 2, 3, // そのまま
     4, 5, 6, 7,
     8, 9,10,11,
     12,13,14,15,}, 
   { 3, 7,11,15, // 回転 左90度
     2, 6,10,14,
     1, 5, 9,13,
     0, 4, 8,12,},
   {15,14,13,12, // 回転 左180度
    11,10, 9, 8,
    7, 6, 5, 4,
    3, 2, 1, 0,},
   {12, 8, 4, 0, // 回転 左270度
    13, 9, 5, 1,
    14,10, 6, 2,
    15,11, 7, 3,},
   {12,13,14,15, // 線対称 軸0度
    8, 9,10,11,
    4, 5, 6, 7,
    0, 1, 2, 3,},
   {15,11, 7, 3, // 線対称 軸45度
    14,10, 6, 2,
    13, 9, 5, 1,
    12, 8, 4, 0,},
   { 3, 2, 1, 0, // 線対称 軸90度
     7, 6, 5, 4,
     11,10, 9, 8,
     15,14,13,12,},
   { 0, 4, 8,12, // 線対称 軸135度
     1, 5, 9,13,
     2, 6,10,14,
     3, 7,11,15}};

unsigned long long hashvalue(int depth, const board_t& board)
{
  unsigned long long minh = 0xffffffffffffffffLL;
UNROLL_PRAGMA(8)
  for (int r = 0; r < 8; r++) {
    unsigned long long h = key_depth[depth];
UNROLL_PRAGMA(16)
    for (int i = 0; i < 16; i++) {
      h ^= key_tile[i][board[symmetric[r][i]]];
    }
    minh = min(minh, h);
  }
 return minh;
//   unsigned long long h = key_depth[depth];
// UNROLL_PRAGMA(16)
//   for (int i = 0; i < 16; i++) {
//     h ^= key_tile[i][board[i]];
//   }
//  return h;
}

// ステージ判定関数：STAGE_THRESHOLD以上の値があるかでステージを決定
inline int get_stage(const board_t &board) {
  UNROLL_PRAGMA(16)
  for (int i = 0; i < 16; i++) {
    if (board[i] >= STAGE_THRESHOLD) {
      return 1; // 高ステージ
    }
  }
  return 0; // 低ステージ
}

// フィルターで評価値を合計する関数
int calcEvFiltered(const board_t &board) {
  int ev = 0;
  int stage = get_stage(board);
  board_t filtered_boards[NUM_SPLIT];
  
  // 各フィルターを適用 (mapping 方式)
  apply_filters_from_mapping(board, filtered_boards);
  UNROLL_PRAGMA(UNROLL_COUNT)
  for (int i = 0; i < UNROLL_COUNT; i++) {
      UNROLL_PRAGMA(NUM_SPLIT)
      for (int f = 0; f < NUM_SPLIT; f++) {
        int index = 0;
        UNROLL_PRAGMA(TUPLE_SIZE)
        for (int k = 0; k < TUPLE_SIZE; k++) {
          index = index * VARIATION_TILE + filtered_boards[f][posSym[i][k]];
        }
	int base = i % NUM_TUPLE;
	int val = Evs[stage][f][base][index];
	ev += val;
      }
  }
  return ev;
}

inline int min(int a, int b) {
  return (a < b) ? a : b;
}

inline int getIndex(const board_t &board, int tuple_id) {
  int index = 0;                 // 現在のボードのタプルのインデックス
  UNROLL_PRAGMA(TUPLE_SIZE)
  for (int j = 0; j < TUPLE_SIZE; j++) {
    const int tile = min(board[posSym[tuple_id][j]], VARIATION_TILE);
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
  UNROLL_PRAGMA(NUM_TUPLE)
  for (int i = 0; i < NUM_TUPLE; i++) {
    tiles[i] = index % VARIATION_TILE;
    index /= VARIATION_TILE;
  }
  int smaller = 0;
  UNROLL_PRAGMA(NUM_TUPLE)
  for (int i = NUM_TUPLE - 1; i >= 0; i--) {
    smaller = smaller * VARIATION_TILE + (tiles[i] > 0 ? (tiles[i] - 1) : 0);
  }
  return smaller;
}

void TDPlayer::gameStart()
{
  firstTurn = true;
}

int expectimaxPlay(int depth, const board_t& board, unordered_map<unsigned long long, int>& ev_table)
{
  if (depth <= 1) { return calcEvFiltered(board); }

  // printf("before hashvalue()\n");
  unsigned long long h = hashvalue(depth, board);
  // printf("after hashvalue() = %lld\n", h);
  if (ev_table.contains(h)) {
    // printf("after contains yes\n");
    return ev_table[h];
  }
  
// printf("after contains no\n");

  int count = 0;
  long long sumScore = 0;
  // すべての場所に置いてみる
  for (int index = 0; index < 16; index++) {
    if (board[index] != 0) continue;
    count++;
    board_t putBoard; board_t nextBoard; int score;
    for (int i = 0; i < 16; i++) putBoard[i] = board[i];
    putBoard[index] = 1;
    int maxScore = -100000;
    for (int d = 0; d < 4; d++) {
      score = moveB(putBoard, nextBoard, (enum move_dir)d);
      if (score == -1) continue;
      int sc = expectimaxPlay(depth-1, nextBoard, ev_table) + (score << 10);
      // printf("depth %d index %d num 1 d %d ==> %d  (cur. %d)\n", depth-1, index, d, sc, maxScore);
      if (maxScore < sc) maxScore = sc;
    }
    sumScore += (long long)maxScore * 9;

    putBoard[index] = 2;
    maxScore = -100000;
    for (int d = 0; d < 4; d++) {
      score = moveB(putBoard, nextBoard, (enum move_dir)d);
      if (score == -1) continue;
      int sc = expectimaxPlay(depth-1, nextBoard, ev_table) + (score << 10);
      // printf("depth %d index %d num 2 d %d ==> %d\n", depth-1, index, d, sc);
      if (maxScore < sc) maxScore = sc;
    }
    sumScore += maxScore;
  }
  // printf("returning depth %d score %d\n", depth, (int)(sumScore / 10 / count));
  ev_table[h] = (count == 0) ? 0 : sumScore / 10 / count;
  return ev_table[h];
}

enum move_dir TDPlayer::selectHandExpectimax(const board_t &/* board */,
					     const alldir_bool &canMoves,
					     const alldir_board &nextBoards,
					     const alldir_int &scores,
					     int depth,
					     unordered_map<unsigned long long, int>& ev_table)
{
  // 評価値の計算（フィルター合計版）
  int nextEv[4] = {0,0,0,0};
  for (int i = 0; i < 4; i++) {   // 方向ごと
    if (!canMoves[i]) continue; // 移動できない場合はスキップ
    nextEv[i] = expectimaxPlay(depth, nextBoards[i], ev_table) + (scores[i] << 10);
    #ifdef DEBUG_PLAY
    printf("move = %d, ev = %d\n", i, nextEv[i]);
    #endif
  }
  // printf("nextEvs: %d %d %d %d\n", nextEv[0], nextEv[1], nextEv[2], nextEv[3]);
  
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
  // printf("selected %d\n", selected);
  return (enum move_dir)selected;
}
void TDPlayer::gameEnd()
{
}
