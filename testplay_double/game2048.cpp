#include <cstdio>
#include <iostream>
using namespace std;

#include "game2048.h"

/**
 * Core routines of game 2048
 *   A board is represented by an array of integer (int[16])
 *   Direction is represented by an integer (0..3)
 *   Each number is represented by its exponent (except for 0 meaning 0).
 */

#define TILECOUNT 18
#define TILECOUNT_LINE (TILECOUNT * TILECOUNT * TILECOUNT * TILECOUNT)
int exp2int[] = {0, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024,
		 2048, 4096, 8192, 16384, 32768, 65536, 131072};

int moveTable[TILECOUNT_LINE];
bool isMovedTable[TILECOUNT_LINE];
int scoreTable[TILECOUNT_LINE];

void init_movetable() {
  for (int org = 0; org < TILECOUNT_LINE; org++) {
    int orgArray[4];
    int n = org; for (int i = 0; i < 4; i++) { orgArray[i] = n % TILECOUNT; n /= TILECOUNT; }

    int from = 1, to = 0, score = 0; bool isMoved = false;
    while (from < 4) {
      if (from == to) {
	from++;
	continue;
      }
      if (orgArray[from] == 0) {
	from++;
	continue;
      }
      if (orgArray[to] == 0) {
	orgArray[to] = orgArray[from]; orgArray[from] = 0;
	isMoved = true;
	from++;
	continue;
      }
      if (orgArray[from] != orgArray[to]) {
	to++;
      } else {
	isMoved = true; score += exp2int[orgArray[to]] * 2;
	orgArray[to]++; orgArray[from] = 0; 
	from++; to++;
      }
    }

    n = 0; for (int i = 4-1; i >= 0; i--) { n = n * TILECOUNT + orgArray[i]; }
    moveTable[org] = n;
    isMovedTable[org] = isMoved;
    scoreTable[org] = score;
  }
}

void printBoard(const board_t &board) {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      printf("%6d ", exp2int[board[y*4+x]]);
    }
    printf("\n");
  }
}

inline int packToInt(int a, int b, int c, int d) {
  return ((a * TILECOUNT + b) * TILECOUNT + c) * TILECOUNT + d;
}
inline void unpackFromInt(int r, int &a, int &b, int &c, int &d) {
  d = r % TILECOUNT; r /= TILECOUNT;
  c = r % TILECOUNT; r /= TILECOUNT;
  b = r % TILECOUNT; r /= TILECOUNT;
  a = r % TILECOUNT; r /= TILECOUNT;
}

int moveUP(const board_t orgBoard, board_t newBoard) {
  int score = 0; bool isMoved = false;
  for (int x = 0; x < 4; x++) {
    int org = packToInt(orgBoard[x + 12], orgBoard[x + 8], orgBoard[x + 4], orgBoard[x + 0]);
    int n = moveTable[org];
    unpackFromInt(n, newBoard[x + 12], newBoard[x + 8], newBoard[x + 4], newBoard[x + 0]);
    score += scoreTable[org];
    isMoved = isMoved || isMovedTable[org];
  }
  if (!isMoved) return -1;
  return score;
}

int moveRIGHT(const board_t orgBoard, board_t newBoard) {
  int score = 0; bool isMoved = false;
  for (int y = 0; y < 16; y+=4) {
    int org = packToInt(orgBoard[y], orgBoard[y+1], orgBoard[y+2], orgBoard[y+3]);
    int n = moveTable[org];
    unpackFromInt(n, newBoard[y], newBoard[y+1], newBoard[y+2], newBoard[y+3]);
    score += scoreTable[org];
    isMoved = isMoved || isMovedTable[org];
  }
  if (!isMoved) return -1;
  return score;
}

int moveDOWN(const board_t orgBoard, board_t newBoard) {
  int score = 0; bool isMoved = false;
  for (int x = 0; x < 4; x++) {
    int org = packToInt(orgBoard[x + 0], orgBoard[x + 4], orgBoard[x + 8], orgBoard[x + 12]);
    int n = moveTable[org];
    unpackFromInt(n, newBoard[x + 0], newBoard[x + 4], newBoard[x + 8], newBoard[x + 12]);
    score += scoreTable[org];
    isMoved = isMoved || isMovedTable[org];
  }
  if (!isMoved) return -1;
  return score;
}

int moveLEFT(const board_t orgBoard, board_t newBoard) {
  int score = 0; bool isMoved = false;
  for (int y = 0; y < 16; y+=4) {
    int org = packToInt(orgBoard[y+3], orgBoard[y+2], orgBoard[y+1], orgBoard[y+0]);
    int n = moveTable[org];
    unpackFromInt(n, newBoard[y+3], newBoard[y+2], newBoard[y+1], newBoard[y+0]);
    score += scoreTable[org];
    isMoved = isMoved || isMovedTable[org];
  }
  if (!isMoved) return -1;
  return score;
}

int moveB(const board_t orgBoard, board_t newBoard, enum move_dir dir) {
  switch (dir) {
  case UP:    return moveUP(orgBoard, newBoard);
  case RIGHT: return moveRIGHT(orgBoard, newBoard);
  case DOWN:  return moveDOWN(orgBoard, newBoard);
  case LEFT:  return moveLEFT(orgBoard, newBoard);
  }
  return -1;
}

/** test code */
#if 0
int main() {
  printf("Game 2048 Core Routine tests\n");
  init_movetable();
		

  board_t newBoard;
	
  board_t initBoard = {0, 2, 4, 3,
		       1, 3, 2, 3,
		       1, 2, 2, 3,
		       8, 8, 2, 3};
  printBoard(initBoard);

  printf("UP,    score: %d\n", moveB(initBoard, newBoard, UP));
  printBoard(newBoard);
  printf("DOWN,  score: %d\n", moveB(initBoard, newBoard, DOWN));
  printBoard(newBoard);
  printf("LEFT,  score: %d\n", moveB(initBoard, newBoard, LEFT));
  printBoard(newBoard);
  printf("RIGHT, score: %d\n", moveB(initBoard, newBoard, RIGHT));
  printBoard(newBoard);

  return 0;
}
#endif
