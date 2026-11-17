"""Dry-run by default; all translation units use identical flags per lock."""
from pathlib import Path
import argparse, hashlib, json, os, socket, subprocess

R = Path(__file__).resolve().parent
UNITS = ('game2048', 'symmetric', 'tdplayer_VSE_exploration', 'playgame_VSE_exploration')

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def plan(digits, kind):
    source = R / 'source'
    flags = ['g++', '-std=c++20', '-O3', '-pthread', '-mcmodel=large',
             '-I' + str(source / 'head_double'), '-DTUPLE_FILE_TYPE=9',
             '-DNT9_COMPACT_STORAGE', '-DNT9_ADAPTIVE_MUTEX', '-DNT9_AOS_PROBE',
             '-DFEATURE_INDEX_DELTA', '-DPROBE_READ_AHEAD=32',
             '-DEXPLORATION_SOURCE_SHA256="' + (R / 'source_sha256.txt').read_text().strip() + '"']
    if digits != 9:
        flags += ['-DNT9_STORAGE_TEST_DIGITS=' + str(digits)]
    flags += (['-DNT9_FUTEX_MUTEX', '-DNT9_FUTEX_SPIN=8'] if kind == 'futex8'
              else ['-DNT9_TTAS_MUTEX', '-DNT9_TTAS_YIELD_SPINS=64'])
    obj = R / 'build/obj' / str(digits) / kind
    objects = [obj / (u + '.o') for u in UNITS]
    commands = [flags + ['-c', str(source / 'learn_double' / (u + '.cpp')), '-o', str(o)] for u, o in zip(UNITS, objects)]
    commands += [flags + [str(o) for o in objects] + ['-o', str(R / 'build/bin' / f'learn{digits}_{kind}')]]
    commands += [flags + ['-c', str(source / 'tools/exploration_campaign/bench_driver.cpp'), '-o', str(obj / 'bench_driver.o')]]
    commands += [flags + [str(o) for o in objects[:3]] + [str(obj / 'bench_driver.o'), '-o', str(R / 'build/bin' / f'bench{digits}_{kind}')]]
    return {'flags': flags, 'commands': commands}

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--digits', type=int, choices=(3, 9), required=True)
    p.add_argument('--run', action='store_true')
    a = p.parse_args()
    source = R / 'source'
    m = ''.join(sha(q) + '  ' + str(q.relative_to(source)) + '\n' for q in sorted(source.rglob('*')) if q.is_file())
    if m != (R / 'source_manifest.sha256').read_text() or hashlib.sha256(m.encode()).hexdigest() != (R / 'source_sha256.txt').read_text().strip():
        raise RuntimeError('source snapshot changed')
    plans = {k: plan(a.digits, k) for k in ('futex8', 'ttas64')}
    if not a.run:
        print(json.dumps({'mode': 'dry-run', 'source_sha256': (R / 'source_sha256.txt').read_text().strip(), 'plans': plans}, indent=2))
        return
    if socket.gethostname().split('.')[0] not in ('i6', 'i06') or not os.sched_getaffinity(0).issubset({0, 1, 2, 3}):
        raise RuntimeError('build requires assigned i06 CPUs 0-3; use taskset')
    if os.getloadavg()[0] > 4:
        raise RuntimeError('host is no longer low load; recheck other jobs')
    dest = R / f'BUILD{a.digits}.json'
    if dest.exists():
        raise RuntimeError('build record exists; preserve it')
    (R / 'build/bin').mkdir(parents=True, exist_ok=True)
    for kind, info in plans.items():
        obj = R / 'build/obj' / str(a.digits) / kind
        obj.mkdir(parents=True, exist_ok=False)
        (obj / 'commands.json').write_text(json.dumps(info['commands'], indent=2) + '\n')
        with (obj / 'build.log').open('w') as log:
            for command in info['commands']:
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    record = {'source_sha256': (R / 'source_sha256.txt').read_text().strip(),
              'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
              'all_translation_units_same_flags_per_variant': True, 'plans': plans,
              'binaries': {q.name: sha(q) for q in (R / 'build/bin').glob(f'*{a.digits}_*')},
              'host': socket.gethostname(), 'affinity': sorted(os.sched_getaffinity(0))}
    dest.write_text(json.dumps(record, indent=2) + '\n')

if __name__ == '__main__':
    main()
