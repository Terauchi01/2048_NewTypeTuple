#ifndef __GAME2048_H__
#define __GAME2048_H__

typedef int board_t[16];
enum move_dir {
  UP = 0,
  RIGHT= 1,
  DOWN = 2,
  LEFT = 3,
};

void init_movetable();
void printBoard(const board_t &board);
int moveB(const board_t orgBoard, board_t newBoard, enum move_dir dir);

inline int biggestTile(const board_t& board) {
  int biggestTile = -1;
  for (int i = 0; i < 16; i++) { if (board[i] > biggestTile) biggestTile = board[i]; }
  return biggestTile;
}

#endif // __GAME2048_H__
