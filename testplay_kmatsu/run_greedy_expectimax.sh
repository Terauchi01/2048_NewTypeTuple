#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
EVFILE="${1:-$REPO_ROOT/learn/dat/tuples6-seed0-VSE-count004.dat}"
RUN_DIR="${RUN_DIR:-$SCRIPT_DIR/results/greedy_expectimax_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$RUN_DIR"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
finish() {
  local rc=$?
  if [[ $rc -eq 0 ]]; then
    echo COMPLETED > "$RUN_DIR/STATUS"
  else
    echo "FAILED exit_code=$rc" > "$RUN_DIR/STATUS"
  fi
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
echo "RUNNING pid=$$" > "$RUN_DIR/STATUS"
echo "$$" > "$RUN_DIR/PID"
exec >> "$RUN_DIR/run.log" 2>&1
if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
  make -C "$SCRIPT_DIR" build-evaluator-audit
fi
echo "evaluator=$EVFILE seeds=0..9 greedy_depth=1 expectimax_depths=3,4"
"$SCRIPT_DIR/audit_evaluator_6" games "$EVFILE" "$RUN_DIR"
python3 - "$RUN_DIR" <<'PY'
import csv,pathlib,sys
root=pathlib.Path(sys.argv[1])
with (root/'summary.csv').open('w') as file:
    out=csv.writer(file)
    out.writerow(['method','games','avg_score','avg_max_tile','avg_moves','total_search_time_us'])
    for method in ('greedy','expectimax3','expectimax4'):
        rows=[]
        for seed in range(10):
            cells=(root/f'{method}_seed{seed}.csv').read_text().strip().split(',')
            row=dict(zip(cells[::2],cells[1::2]))
            if row.get('status')!='terminal':
                raise RuntimeError(f'{method} seed {seed} did not finish')
            rows.append(row)
        out.writerow([method,len(rows),sum(int(r['sco']) for r in rows)/10,
                      sum(2**int(r['big']) for r in rows)/10,
                      sum(int(r['moves']) for r in rows)/10,
                      sum(int(r['search_time_us']) for r in rows)])
PY
