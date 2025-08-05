#include <cstdio>
#include <random>
using namespace std;
#include "game2048.h"
#include "symmetric.h"
#include "tdplayer2symmetric.h"
#include "util.h"

#define VARIATION_TILE 16
#define TUPLE_SIZE 6
#define NUM_TUPLE 8
// #define NUM_TUPLE 10
#define ARRAY_LENGTH (VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE * VARIATION_TILE)

#define EV_INIT_VALUE 0

int Evs[NUM_TUPLE][ARRAY_LENGTH];
int pos[NUM_TUPLE][TUPLE_SIZE];

//#include "selected_7_tuples.h"
#include "all_6_tuples.h"

int posSym[NUM_TUPLE][8][TUPLE_SIZE];

void init_tuple(char** argv) {
  int order[AVAIL_TUPLE];
  for (int i = 0; i < NUM_TUPLE; i++) {
    order[i] = atoi(argv[2+i]);
  }
  for (int i = 0; i < NUM_TUPLE; i++) {
    for (int k = 0; k < TUPLE_SIZE; k++) {
      pos[i][k] = tuples[order[i]][k];
    }
  }
  
  printf("TDPlayer 2 Symmetric ver.\n");
  for (int i = 0; i < NUM_TUPLE; i++) {
    printf(" %2d-th-tuples: %03d [%2d", (i+1), order[i], pos[i][0]);
    for (int j = 1; j < TUPLE_SIZE; j++) printf(",%2d", pos[i][j]);
    printf("]\n");
  }

  // �Ώ̐��̍l�� posSym �̏���
  for (int i = 0; i < NUM_TUPLE; i++) {
    for (int j = 0; j < 8; j++) {
      for (int k = 0; k < TUPLE_SIZE; k++) {
	posSym[i][j][k] = symmetricPos[j][pos[i][k]];
      }
    }
  }
  
  // �]���l�̏�����: ����� 0 ������
  for (int i = 0; i < NUM_TUPLE; i++) {
    for (int j = 0; j < ARRAY_LENGTH; j++) {
      Evs[i][j] = EV_INIT_VALUE;
    }
  }
}

void output_ev(int suffix) {
  char filename[1024];
  FILE *fp;
  sprintf(filename, "tuples%d-%d-%d.dat", TUPLE_SIZE, NUM_TUPLE, suffix);
  fp = fopen(filename, "wb");
  fwrite(Evs, sizeof(int)*ARRAY_LENGTH, NUM_TUPLE, fp);
  fclose(fp);
}
  
inline int min(int a, int b) {
  return (a < b) ? a : b;
}

inline int getIndexSym(const board_t &board, int tuple_id, int sym_id) {
  int index = 0;                 // ���݂̃{�[�h�̃^�v���̃C���f�b�N�X
  for (int j = 0; j < TUPLE_SIZE; j++) {
    const int tile = min(board[posSym[tuple_id][sym_id][j]], VARIATION_TILE);
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
  for (int i = 0; i < NUM_TUPLE; i++) {
    tiles[i] = index % VARIATION_TILE; index /= VARIATION_TILE;
  }
  int smaller = 0;
  for (int i = NUM_TUPLE - 1; i >= 0; i--) {
    smaller = smaller * VARIATION_TILE + (tiles[i] > 0 ? (tiles[i] - 1) : 0);
  }
  return smaller;
}

// (���ׂĂ̑����ȔՖʂ��܂�)����Ֆʂł̃^�v���̏o�͂̍��v��Ԃ�
inline int calcEv(const board_t &board)
{
  int ev = 0;
  for (int k = 0; k < NUM_TUPLE; k++) { // �^�v�����̃��[�v
    for (int sym = 0; sym < 8; sym++) {
      const int index = getIndexSym(board, k, sym);
      if (Evs[k][index] == EV_INIT_VALUE) {
	// �����l����菬���ȃ^�C���̏ꍇ�̕]���l�����ƂɍX�V����
	const int smaller_index = getSmallerIndex(index);
	Evs[k][index] = Evs[k][smaller_index];
      }
      ev += Evs[k][index]; // �ŏI�I�ȕ]���l�ɑ���
    }
  }
  return ev;
}

// �w�K�̂��߂̊֐��Q
static void learningUpdate(const board_t& before, int delta)
{
  for (int k = 0; k < NUM_TUPLE; k++) { // �^�v�����̃��[�v
    for (int sym = 0; sym < 8; sym++) { // ��]��Ώ̂ȔՖʖ��̃��[�v
      const int index = getIndexSym(before, k, sym);
      Evs[k][index] += delta;
    }
  }
}  
void learning(const board_t &before, const board_t &after, int rewards)
{
  const int thisEvV = calcEv(before);
  const int nextEvV = calcEv(after);
  const int delta = rewards + ((nextEvV - thisEvV) >> 10); // V(S)��V'(S)�̌덷 �� 1/1024
  learningUpdate(before, delta);
}  
void learningLast(const board_t &before)
{
  const int thisEvV = calcEv(before);
  const int delta = 0 + ((0 - thisEvV) >> 10); // V(S)��V'(S)�̌덷 �� 1/1024
  learningUpdate(before, delta);
}

void TDPlayer::gameStart()
{
  firstTurn = true;
}

enum move_dir TDPlayer::selectHand(const board_t &/* board */,
				   const alldir_bool &canMoves,
				   const alldir_board &nextBoards,
				   const alldir_int &scores)
{
  // �]���l�̌v�Z
  int nextEv[4];
  for (int i = 0; i < 4; i++) {   // ������
    if (!canMoves[i]) continue; // �������Ȃ���Ζ���
    nextEv[i] = calcEv(nextBoards[i]) + (scores[i] << 10);
  }
  // �ő�̕]���l��I��
  int maxi = -1; int maxv = 0;
  for (int i = 0; i < 4; i++) { // ������
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
  
  // �I�񂾎�ɂ���Ċw�K
  if (firstTurn) {
    firstTurn = false;
  } else {
    learning(lastBoard, nextBoards[selected], scores[selected]);
  }

  // lastBoard ���X�V
  for (int i = 0; i < 16; i++) {
    lastBoard[i] = nextBoards[selected][i];
  }

  return (enum move_dir)selected;
  
}

void TDPlayer::gameEnd()
{
  learningLast(lastBoard);
}

#if 0
// test code
int main() {
  init_movetable();
  init_tuple();
  printf("initialization finished\n");

  return 0;
}
#endif
