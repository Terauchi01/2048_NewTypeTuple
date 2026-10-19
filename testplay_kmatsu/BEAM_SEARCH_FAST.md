# Separate fast Beam Search implementation

`beam_search_fast.cpp` is an experimental implementation kept alongside the
existing `beam_search.cpp`. It exports `beam2048::search_fast`; the existing
`beam2048::search` implementation and its callers are unchanged.

The fast implementation uses `std::nth_element` to select the exact same
deterministic top-W prefix, sorts only the retained candidates, reuses
frontier buffers across layers, and emits spawn outcomes directly without
allocating a temporary spawn vector for every expanded board. It keeps the
same value and generation-order tie break as the baseline.

Build the fast runner and run its copy of the baseline unit tests with:

```sh
bash testplay_kmatsu/build_beam_fast.sh
```

Compare both implementations with the same evaluator, seeds, depth, width and
move limit using:

```sh
bash testplay_kmatsu/compare_beam_fast.sh tuples6-seed0-VSE-count004.dat
```

The default comparison runs 10 games at depth 5, fixed width 128, and at most
20 player moves per game. Set `GAME_COUNT`, `DEPTH`, `MAX_MOVES`, `METHOD`,
`MAX_WIDTH`, `K`, or `RESULT_DIR` to adjust the run. The timestamped result
directory contains each runner's per-game CSV and a timing summary. Search
values, node counts, game scores and moves should match; wall-clock times are
expected to differ.
