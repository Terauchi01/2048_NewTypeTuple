#!/usr/bin/env bash
set -euo pipefail
r=$(cd "$(dirname "$0")" && pwd);digits=${1:?3 or 9};[[ $digits == 3 || $digits == 9 ]] || exit 1
src="$r/source";read -r hash < "$r/source_sha256.txt";out="$r/eval_build/$digits";[[ ! -e "$out" ]] || { echo 'output exists' >&2;exit 2; };mkdir -p "$out"
flags=(-std=c++20 -O3 -pthread -mcmodel=large -I"$src/head_double" -DTUPLE_FILE_TYPE=9 -DNT9_COMPACT_STORAGE -DNT9_FLOAT32_STORAGE -DNT9_AOS_PROBE -DNT9_ADAPTIVE_MUTEX -DFEATURE_INDEX_DELTA -DPROBE_READ_AHEAD=32 "-DEXPLORATION_SOURCE_SHA256=\"$hash\"" -DNT9_TTAS_MUTEX -DNT9_TTAS_YIELD_SPINS=64)
[[ "$digits" == 9 ]] || flags+=(-DNT9_STORAGE_TEST_DIGITS="$digits")
printf '%q ' g++ "${flags[@]}" > "$out/compile_flags.txt";printf '\n' >> "$out/compile_flags.txt"
for unit in game2048 symmetric tdplayer_VSE_exploration;do g++ "${flags[@]}" -c "$src/learn_double/$unit.cpp" -o "$out/$unit.o";done
g++ "${flags[@]}" -c "$r/evaluate.cpp" -o "$out/evaluate.o"
g++ "${flags[@]}" "$out"/{game2048,symmetric,tdplayer_VSE_exploration,evaluate}.o -o "$out/evaluate"
sha256sum "$r/evaluate.cpp" "$out/evaluate" > "$out/SHA256SUMS"
