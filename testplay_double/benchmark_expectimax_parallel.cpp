#define EXPECTIMAX_NO_MAIN
#define EXPECTIMAX_PARALLEL_ROOT
#include "test_expectimax.cpp"

int main(int argc, char** argv)
{
  if (argc != 3) {
    fprintf(stderr, "usage: %s DEPTH EVFILE\n", argv[0]);
    return 2;
  }

  const int depth = atoi(argv[1]);
  const char* filename = argv[2];

  init_movetable();
  input_ev(filename);

  // The initial board from seed 900200, where the full-game comparison first
  // diverged, is used to catch root-selection differences directly.
  board_t board = {
       0,  0,  1,  0,
       0,  0,  0,  0,
       0,  0,  0,  0,
       0,  0,  1,  0
  };

  alldir_bool can_moves;
  alldir_board next_boards;
  alldir_int scores;
  for (int direction = 0; direction < 4; direction++) {
    scores[direction] = moveB(
        board, next_boards[direction], static_cast<enum move_dir>(direction));
    can_moves[direction] = scores[direction] > -1;
  }

  unordered_map<unsigned long long, double> sequential_table;
  shared_mutex sequential_table_mutex;
  unordered_map<unsigned long long, double> parallel_table;
  shared_mutex parallel_table_mutex;

  const auto sequential_start = chrono::steady_clock::now();
  const int sequential_direction = selectHandExpectimaxCanonicalSequential(
      can_moves,
      next_boards,
      scores,
      depth,
      sequential_table,
      sequential_table_mutex);
  const auto sequential_end = chrono::steady_clock::now();

  const auto parallel_start = chrono::steady_clock::now();
  const int parallel_direction = selectHandExpectimaxParallel(
      can_moves,
      next_boards,
      scores,
      depth,
      parallel_table,
      parallel_table_mutex);
  const auto parallel_end = chrono::steady_clock::now();

  const double sequential_seconds =
      chrono::duration<double>(sequential_end - sequential_start).count();
  const double parallel_seconds =
      chrono::duration<double>(parallel_end - parallel_start).count();

  printf("depth=%d sequential_dir=%d parallel_dir=%d "
         "sequential_seconds=%.6f parallel_seconds=%.6f speedup=%.3f "
         "sequential_entries=%zu parallel_entries=%zu\n",
         depth,
         sequential_direction,
         parallel_direction,
         sequential_seconds,
         parallel_seconds,
         sequential_seconds / parallel_seconds,
         sequential_table.size(),
         parallel_table.size());

  return sequential_direction == parallel_direction ? 0 : 1;
}
