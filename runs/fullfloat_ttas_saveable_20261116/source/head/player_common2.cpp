inline int computeStage(const board_t &board, int turn)
{
  int stage = 0;
#ifdef STAGED_MAX
  stage = min(max(biggestTile(board) - 16 + NUM_STAGE, 0), NUM_STAGE-1);
#endif
#ifdef STAGED_TURN
  stage = min(turn / 4096, NUM_STAGE-1);
#endif
#ifdef STAGED_TURN4
  stage = min(turn / 8192, NUM_STAGE-1);
#endif
#ifdef STAGED_BY13
  for (int j = 0; j < 16; j++) {
    switch (board[j]) {
    case 13: stage |= 1; break;
    case 14: stage |= 2; break;
    case 15: stage |= 4; break;
    case 16:
    case 17:
      stage |= 7;
    }
  }
  stage = min(stage, NUM_STAGE-1);
#endif
#ifdef STAGED_BY14
  for (int j = 0; j < 16; j++) {
    switch (board[j]) {
    case 14: stage |= 1; break;
    case 15: stage |= 2; break;
    case 16:
    case 17:
	stage |= 3; break;
    }
  }
  stage = min(stage, NUM_STAGE-1);
#endif
#ifdef STAGED_WAVE
  for (int j = 0; j < 16; j++) {
    switch (board[j]) {
    case 12: stage |= 1; break;
    case 13: stage |= 2; break;
    case 14: stage |= 4; break;
    case 15: stage |= 8; break;
    case 16:
    case 17: stage |= 16; break;
    }
  }
  switch (stage) {
  case  0: case  1: case  2: case  3: stage = 0; break;
  case  4: case  5:                   stage = 1; break;
  case  6:                            stage = 2; break;
  case  7:                            stage = 3; break;
  case  8: case  9: case 10: case 11: stage = 4; break;
  case 12: case 13:                   stage = 5; break;
  case 14:                            stage = 6; break;
  default:                            stage = 7; break;
  }
  stage = min(stage, NUM_STAGE-1);
#endif
#ifdef STAGED_DECR
  for (int j = 0; j < 16; j++) {
    switch (board[j]) {
    case  9: stage |= 1; break;
    case 10: stage |= 2; break;
    case 11: stage |= 4; break;
    case 12: stage |= 8; break;
    case 13: stage |= 16; break;
    case 14: stage |= 32; break;
    case 15: stage |= 64; break;
    case 16:
    case 17: stage |= 128; break;
    }
  }
  if (stage < 64) stage = 0;
  else if (stage < 96) stage = 1;
  else if (stage < 112) stage = 2;
  else if (stage < 120) stage = 3;
  else if (stage < 124) stage = 4;
  else if (stage < 126) stage = 5;
  else if (stage < 127) stage = 6;
  else stage = 7;
  stage = min(stage, NUM_STAGE-1);
#endif
  return stage;
}

int expectimaxPlay(int depth, const board_t& board, int cturn)
{
  if (depth <= 0) { return calcEv(board, computeStage(board, cturn)); }

  int count = 0;
  long long sumScore = 0;
  // ‚·‚×‚Ä‚ÌêŠ‚É’u‚¢‚Ä‚Ý‚é
  for (int index = 0; index < 16; index++) {
    if (board[index] != 0) continue;
    count++;
    board_t putBoard; board_t nextBoard; int score;
    copyBoard(board, putBoard); putBoard[index] = 1;
    int maxScore = 0;
    for (int d = 0; d < 4; d++) {
      score = moveB(putBoard, nextBoard, (enum move_dir)d);
      if (score == -1) continue;
      int sc = expectimaxPlay(depth-1, nextBoard, cturn+1) + (score << 10);
      if (maxScore < sc) maxScore = sc;
    }
    sumScore += maxScore * 9;

    putBoard[index] = 2;
    maxScore = 0;
    for (int d = 0; d < 4; d++) {
      score = moveB(putBoard, nextBoard, (enum move_dir)d);
      if (score == -1) continue;
      int sc = expectimaxPlay(depth-1, nextBoard, cturn+1) + (score << 10);
      if (maxScore < sc) maxScore = sc;
    }
    sumScore += maxScore;
  }
  if (count == 0) return 0;
  return sumScore / 10 / count;
}
