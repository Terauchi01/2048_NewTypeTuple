"""Print reviewed D trial commands only. Execution belongs to the guarded runner."""
from pathlib import Path
import argparse, json

R = Path(__file__).resolve().parent
PROJECT = R.parent.parent

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--variant', choices=('old_d', 'precision50', 'futex8', 'ttas64'), required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--steps', type=int, default=15000000)
    p.add_argument('--threads', type=int, choices=(1, 15), default=15)
    p.add_argument('--seed', type=int, default=7)
    p.add_argument('--bench', action='store_true')
    a = p.parse_args()
    if a.steps <= 0 or (a.bench and a.steps > 20000000):
        p.error('positive steps required; diagnostic bench cap is20M')
    if a.output.exists():
        p.error('output already exists')
    if a.variant == 'old_d':
        if a.bench:
            p.error('old D has no matching no-save bench; compare real drivers')
        release = PROJECT / 'runs/precision_10b_20261116'
        binary = release / 'bin/learn_float64'
        build = json.loads((release / 'BUILD_COMPLETE.json').read_text())
    elif a.variant == 'precision50':
        if a.bench:
            p.error('precision50 has no pinned no-save bench; compare real drivers')
        release = PROJECT / 'runs/precision_50b_fast_20261116/build'
        binary = release / 'bin/learn_9_float64_aos'
        record = json.loads((release / 'BUILD.json').read_text())
        build = {'source_sha256': record['source_sha256'], 'binaries': record['variants']['9_float64_aos']['binaries']}
    else:
        binary = R / 'build/bin' / (('bench9_' if a.bench else 'learn9_') + a.variant)
        build = json.loads((R / 'BUILD9.json').read_text()) if (R / 'BUILD9.json').exists() else {'source_sha256': (R / 'source_sha256.txt').read_text().strip(), 'binaries': {binary.name: 'PENDING_BUILD'}}
    command = ['taskset', '-c', '0-14' if a.threads == 15 else '0', str(binary), str(a.seed),
               '--experiment-id', 'd64_source_speed', '--threads', str(a.threads), '--steps', str(a.steps),
               '--behavior', 'tc-ucb', '--formula', 'source', '--ucb-c', '0.0065', '--warmup-steps', '0',
               '--decay-end-steps', '0', '--start-pool-per-million', '0',
               '--model-access', 'async', '--checkpoint-format', 'compact', '--feature-cache-mib', '16',
               '--first-checkpoint-step', str(a.steps), '--checkpoint-interval', '50000000000',
               '--summary-interval', '1000000', '--sample-every', '0', '--retention', 'no',
               '--output-dir', str(a.output.absolute()), '--checkpoint-io-lock', str(a.output.absolute()) + '.io.lock']
    print(json.dumps({'mode': 'plan_only', 'host': 'i06 (recheck idle state before run)',
                      'command': command, 'environment': {'NT9_THP': '1'},
                      'source_sha256': build['source_sha256'], 'binary_sha256': build['binaries'][binary.name],
                      'checkpointing': not a.bench,
                      'required_execution_guard': 'parent-approved idle host, CPU affinity, RAM/cgroup, disk/swap, timeout, own-process watchdog, external checkpoint SHA audit',
                      'comparison': 'Use real driver wave_complete timing for all variants. Initialization/save excluded; in-wave CSV included.'}, indent=2))

if __name__ == '__main__':
    main()
