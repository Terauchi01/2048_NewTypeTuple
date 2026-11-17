#include <cstdio>
using namespace std;
#include "game2048.h"
#include "symmetric.h"

const int symmetricPos[8][16] =
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

void getSymmetricBoard(const board_t &orgBoard, board_t &newBoard, int symNum) {
  for (int i = 0; i < 16; i++) {
    newBoard[i] = orgBoard[symmetricPos[symNum][i]];
  }
}

#if 0
// test code
int main() {
  init_tuple();
  int n, r;
  return 0;
}
#endif
  
