#!/usr/bin/env python3
"""Explicit launcher for the existing full-float32 TTAS research binary.

Dry-run is the default. No prior checkpoint identity is modified.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time

from process_guard import on_parent_death
from resource_guard import available

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
RELEASE = PROJECT / 'runs/fullfloat_ttas_saveable_20261116'
BINARY = RELEASE / 'build/bin/learn9_ttas'
SOURCE = RELEASE / 'source'
SOURCE_SHA = '76428313e6c6728559db14ee84225dca15b2c00cf29bec8103ca3b811e2db11c'
BINARY_SHA = 'd04836898c652a7eb3213298aec399ea5b43c76befeff39b3213ecc80231eabd'
DEFAULT_MANIFEST = PROJECT / 'runs/archive_readiness_20261112/donor_pools/train_manifest.txt'
METHODS = {
    'A': ('tc-ucb', 'paper', None),
    'B': ('greedy', 'paper', 'B'),
    'Bprime': ('greedy', 'paper', 'Bprime'),
    'Bdoubleprime': ('greedy', 'paper', 'Bdoubleprime'),
    'C': ('tc-ucb', 'source', 'C'),
}
GIB = 1024 ** 3
MODEL_CHECKPOINT_BYTES = 20195078649
TC_RESET_STEPS = 50000000000

def is_allowed_host(host):
    return re.fullmatch(r'i(?:0?[2-9]|1[0-2])', host.split('.')[0]) is not None

def scheduled_checkpoint_upper_bound(start, target, first, period):
    """Count distinct configured boundaries, including 50B TC-reset boundaries.

    Episode overshoot can skip a boundary; manual stops and logs are additional.
    This is a storage planning bound, not a disk-space reservation.
    """
    if start < 0 or target <= start or first <= 0 or period <= 0:
        raise ValueError('invalid checkpoint planning range')
    common = math.lcm(period, TC_RESET_STEPS)
    multiples = lambda n: target // n - start // n
    count = multiples(period) + multiples(TC_RESET_STEPS) - multiples(common)
    if start < first <= target and first % period and first % TC_RESET_STEPS:
        count += 1
    if target % period and target % TC_RESET_STEPS and target != first:
        count += 1
    return count

def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for part in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            digest.update(part)
    return digest.hexdigest()

def verify_release():
    expected = json.loads((RELEASE / 'BUILD9.json').read_text())
    lines = ''.join(sha(p) + '  ' + str(p.relative_to(SOURCE)) + '\n'
                    for p in sorted(SOURCE.rglob('*')) if p.is_file())
    if (lines != (RELEASE / 'source_manifest.sha256').read_text()
            or hashlib.sha256(lines.encode()).hexdigest() != SOURCE_SHA
            or (RELEASE / 'source_sha256.txt').read_text().strip() != SOURCE_SHA
            or expected['source_sha256'] != SOURCE_SHA
            or expected['binaries']['learn9_ttas'] != BINARY_SHA
            or sha(BINARY) != BINARY_SHA):
        raise RuntimeError('source/binary identity mismatch')

def swapout_bytes():
    row = next(s for s in Path('/proc/vmstat').read_text().splitlines() if s.startswith('pswpout '))
    return int(row.split()[1]) * os.sysconf('SC_PAGE_SIZE')

def nonnegative(value):
    if not re.fullmatch(r'[0-9]+', value):
        raise argparse.ArgumentTypeError('must be a nonnegative integer')
    return int(value)

def positive(value):
    result = nonnegative(value)
    if result < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return result

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--method', required=True, choices=METHODS)
    p.add_argument('--seed', required=True, type=nonnegative)
    p.add_argument('--steps', required=True, type=positive)
    p.add_argument('--output-dir', required=True, type=Path)
    p.add_argument('--pool-manifest', type=Path)
    p.add_argument('--resume-full', type=Path)
    p.add_argument('--feature-cache-mib', type=int, choices=(16, 32), default=16)
    p.add_argument('--first-checkpoint-step', type=positive, default=1000000)
    p.add_argument('--checkpoint-interval', type=positive, default=50000000000)
    p.add_argument('--sample-every', type=nonnegative, default=0)
    p.add_argument('--checkpoint-io-lock', type=Path,
                   help='shared lock directory for checkpoint I/O; defaults to a run-specific path')
    p.add_argument('--checkpoint-io-timeout-seconds', type=positive, default=1200)
    mode = p.add_mutually_exclusive_group()
    mode.add_argument('--dry-run', action='store_true', help='print verified command (default)')
    mode.add_argument('--run', action='store_true', help='explicitly start the driver')
    a = p.parse_args(argv)
    if a.seed > 2147483647:
        p.error('seed must fit the driver signed integer range')
    if a.checkpoint_io_timeout_seconds > 86400:
        p.error('checkpoint I/O timeout must not exceed 86400 seconds')
    if a.method == 'A' and a.pool_manifest is not None:
        p.error('A uses normal starts and does not accept an archive manifest')
    out = a.output_dir.expanduser().resolve()
    if out.exists():
        p.error('output directory must not already exist')
    if not out.parent.is_dir():
        p.error('output parent must already exist')
    lock = (a.checkpoint_io_lock.expanduser().absolute() if a.checkpoint_io_lock
            else out.with_name(out.name + '.checkpoint_lock'))
    if not lock.parent.is_dir():
        p.error('checkpoint lock parent must already exist')
    if lock.is_symlink() or (lock.exists() and not lock.is_dir()):
        p.error('checkpoint lock must be a directory path, not a file or symlink')
    if a.checkpoint_io_lock is None and lock.exists():
        p.error('run-specific checkpoint lock path already exists')
    if a.resume_full is not None:
        resume = a.resume_full.expanduser().resolve()
        if not resume.is_dir() or not (resume / 'COMPLETE').is_file():
            p.error('resume-full must be a complete checkpoint directory')
        try:
            start_steps = int(json.loads((resume / 'checkpoint.json').read_text())['actual_steps'])
        except (OSError, ValueError, KeyError, TypeError):
            p.error('resume checkpoint metadata is unreadable')
    else:
        resume = None
        start_steps = 0
    try:
        planned_count = scheduled_checkpoint_upper_bound(start_steps, a.steps, a.first_checkpoint_step, a.checkpoint_interval)
    except ValueError as exc:
        p.error(str(exc))
    verify_release()
    behavior, formula, condition = METHODS[a.method]
    decay_end_steps = 0 if a.method in ('A', 'C') else 180000000000
    cmd = [str(BINARY), str(a.seed), '--experiment-id', 'speed32_' + a.method,
           '--threads', '15', '--steps', str(a.steps), '--behavior', behavior,
           '--formula', formula, '--ucb-c', '0.0065', '--warmup-steps', '0',
           '--decay-end-steps', str(decay_end_steps), '--model-access', 'async',
           '--checkpoint-format', 'compact', '--feature-cache-mib', str(a.feature_cache_mib),
           '--first-checkpoint-step', str(a.first_checkpoint_step),
           '--checkpoint-interval', str(a.checkpoint_interval),
           '--summary-interval', '1000000', '--sample-every', str(a.sample_every), '--retention', 'no',
           '--checkpoint-io-lock', str(lock), '--checkpoint-io-timeout-seconds', str(a.checkpoint_io_timeout_seconds),
           '--output-dir', str(out)]
    if condition is None:
        cmd += ['--start-pool-per-million', '0']
    else:
        manifest = (a.pool_manifest or DEFAULT_MANIFEST).expanduser().resolve()
        if not manifest.is_file():
            p.error('archive manifest does not exist')
        cmd += ['--archive-mode', 'random128', '--start-pool-manifest', str(manifest),
                '--start-pool-manifest-sha256', sha(manifest)]
        if condition == 'C':
            cmd += ['--archive-start-policy', 'requests', '--start-pool-per-million', '500000',
                    '--archive-source-update', '1']
        else:
            cmd += ['--archive-condition', condition]
    if resume is not None:
        cmd += ['--resume-full', str(resume)]
    payload = {'method': a.method, 'mode': 'run' if a.run else 'dry-run',
               'source_sha256': SOURCE_SHA, 'binary_sha256': BINARY_SHA,
               'command': cmd, 'host': socket.gethostname(),
               'storage_plan': {'configured_checkpoint_upper_bound': planned_count,
                  'estimated_model_bytes': planned_count * MODEL_CHECKPOINT_BYTES,
                  'additional_headroom_bytes': 12 * GIB,
                  'scope': 'model files only; logs, metadata, failed/manual saves and other runs require additional space; no storage reservation'},
               'limitations': ['TTAS is nonfair; no bounded progress guarantee',
                 'full checkpoint resume requires the identical source, binary and driver identity',
                 'archive request allocation can fall back to normal starts',
                 'independent multi-seed long-term quality equivalence has not been established']}
    print(json.dumps(payload, indent=2), flush=True)
    if not a.run:
        return 0
    host = socket.gethostname().split('.')[0]
    if not is_allowed_host(host):
        raise RuntimeError('run requires an authorized i02–i12 host')
    if os.getloadavg()[0] > 1.5 or not set(range(15)).issubset(os.sched_getaffinity(0)):
        raise RuntimeError('host load or CPU affinity guard')
    required_disk = max(64 * GIB, planned_count * MODEL_CHECKPOINT_BYTES + 12 * GIB)
    if available() < 72 * GIB or shutil.disk_usage(out.parent).free < required_disk:
        raise RuntimeError('insufficient available memory or output disk space')
    env = os.environ.copy()
    env['NT9_THP'] = '1'
    watchdog = HERE / 'group_watchdog.py'
    child = subprocess.Popen([sys.executable, str(watchdog), '--', 'taskset', '-c', '0-14', *cmd],
                             env=env, start_new_session=True, preexec_fn=on_parent_death(os.getpid()))
    initial_swap = swapout_bytes()
    try:
        while child.poll() is None:
            if available() < 12 * GIB or shutil.disk_usage(out.parent).free < 12 * GIB:
                raise RuntimeError('memory/disk safety guard')
            if swapout_bytes() - initial_swap > 64 * 1024 ** 2:
                raise RuntimeError('host swapout safety guard')
            time.sleep(1)
    except BaseException:
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
        raise
    return child.returncode

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as exc:
        print('launch failed: ' + str(exc), file=sys.stderr)
        sys.exit(1)
