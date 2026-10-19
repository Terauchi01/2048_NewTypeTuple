#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

CXX="${CXX:-clang++}"
CXXFLAGS=(-pthread -std=c++20 -W -Wall -O3 -I../head)
if [[ -n "${EXTRA_CXXFLAGS:-}" ]]; then
  # EXTRA_CXXFLAGS is intentionally word-split into compiler arguments.
  CXXFLAGS+=( ${EXTRA_CXXFLAGS} )
fi

"$CXX" "${CXXFLAGS[@]}" -DTUPLE_FILE_TYPE=6 \
  test_beam_search_fast.cpp beam_search_fast.cpp beam_search.cpp \
  double_evaluator.cpp game2048.cpp \
  -o test_beam_search_fast

"$CXX" "${CXXFLAGS[@]}" -UDEBUG_PLAY -DTUPLE_FILE_TYPE=6 \
  test_beam_search_fast_unit.cpp beam_search_fast.cpp beam_search.cpp \
  double_evaluator.cpp game2048.cpp \
  -o beam_search_fast_unit_tests

./beam_search_fast_unit_tests

"$CXX" "${CXXFLAGS[@]}" -UDEBUG_PLAY -DTUPLE_FILE_TYPE=6 \
  test_beam_search_fast_parity.cpp beam_search_fast.cpp beam_search.cpp \
  double_evaluator.cpp game2048.cpp -o beam_search_fast_parity_tests
./beam_search_fast_parity_tests
