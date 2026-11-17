#!/usr/bin/env bash
set -euo pipefail
r=$(cd "$(dirname "$0")" && pwd);digits=${1:?digits}
[[ $digits == 3 || $digits == 9 ]] || exit 1
src="$r/source";read -r hash < "$r/source_sha256.txt"
mkdir -p "$r/build/bin" "$r/build/obj"
[[ ! -e "$r/build/obj/$digits" && ! -e "$r/BUILD$digits.json" ]] || { echo 'output exists' >&2;exit 2; }
mkdir "$r/build/obj/$digits"
for kind in futex ttas;do
 flags=(-std=c++20 -O3 -pthread -mcmodel=large -I"$src/head_double" -DTUPLE_FILE_TYPE=9 -DNT9_COMPACT_STORAGE -DNT9_FLOAT32_STORAGE -DNT9_AOS_PROBE -DNT9_ADAPTIVE_MUTEX -DFEATURE_INDEX_DELTA -DPROBE_READ_AHEAD=32 "-DEXPLORATION_SOURCE_SHA256=\"$hash\"")
 [[ $digits == 9 ]] || flags+=(-DNT9_STORAGE_TEST_DIGITS="$digits")
 if [[ $kind == futex ]];then flags+=(-DNT9_FUTEX_MUTEX);else flags+=(-DNT9_TTAS_MUTEX -DNT9_TTAS_YIELD_SPINS=64);fi
 obj="$r/build/obj/$digits/$kind";mkdir "$obj";printf '%q ' g++ "${flags[@]}" > "$obj/compile_flags.txt";printf '\n' >> "$obj/compile_flags.txt"
 for unit in game2048 symmetric tdplayer_VSE_exploration playgame_VSE_exploration;do g++ "${flags[@]}" -c "$src/learn_double/$unit.cpp" -o "$obj/$unit.o";done
 g++ "${flags[@]}" "$obj"/{game2048,symmetric,tdplayer_VSE_exploration,playgame_VSE_exploration}.o -o "$r/build/bin/learn${digits}_${kind}"
done
python3 - "$r" "$digits" <<'PY'
from pathlib import Path
import hashlib,json,sys,subprocess
r=Path(sys.argv[1]);d=sys.argv[2];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();o={'source_sha256':(r/'source_sha256.txt').read_text().strip(),'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],'all_translation_units_same_flags_per_variant':True,'variant_flags':{v:(r/'build/obj'/d/v/'compile_flags.txt').read_text().strip() for v in ('futex','ttas')},'binaries':{f'learn{d}_{v}':sha(r/'build/bin'/f'learn{d}_{v}') for v in ('futex','ttas')}};(r/f'BUILD{d}.json').write_text(json.dumps(o,indent=2)+'\n')
PY
