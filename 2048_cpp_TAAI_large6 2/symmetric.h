#ifndef __SYMMETRIC_H__
#define __SYMMETRIC_H__

#include "game2048.h"
void getSymmetricBoard(const board_t &orgBoard, board_t &newBoard, int symNum);
extern const int symmetricPos[8][16];

#endif // __SYMMETRIC_H__
