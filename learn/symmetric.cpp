#include <cstdio>
using namespace std;
#include "game2048.h"
#include "symmetric.h"

const int symmetricPos[8][16] =
  {{ 0, 1, 2, 3, // ���̂܂�
     4, 5, 6, 7,
     8, 9,10,11,
     12,13,14,15,}, 
   { 3, 7,11,15, // ��] ��90�x
     2, 6,10,14,
     1, 5, 9,13,
     0, 4, 8,12,},
   {15,14,13,12, // ��] ��180�x
    11,10, 9, 8,
    7, 6, 5, 4,
    3, 2, 1, 0,},
   {12, 8, 4, 0, // ��] ��270�x
    13, 9, 5, 1,
    14,10, 6, 2,
    15,11, 7, 3,},
   {12,13,14,15, // ���Ώ� ��0�x
    8, 9,10,11,
    4, 5, 6, 7,
    0, 1, 2, 3,},
   {15,11, 7, 3, // ���Ώ� ��45�x
    14,10, 6, 2,
    13, 9, 5, 1,
    12, 8, 4, 0,},
   { 3, 2, 1, 0, // ���Ώ� ��90�x
     7, 6, 5, 4,
     11,10, 9, 8,
     15,14,13,12,},
   { 0, 4, 8,12, // ���Ώ� ��135�x
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
  
