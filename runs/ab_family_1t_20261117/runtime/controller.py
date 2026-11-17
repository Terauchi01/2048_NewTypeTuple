#!/usr/bin/env python3
"""One fresh 15-worker learner per assigned host; request-file stops only."""
from pathlib import Path
import argparse
from collections import deque
import fcntl
import hashlib
import json
import os
import re
import select
import signal
import socket
import subprocess
import sys
import time
import uuid
import math

PROJECT = Path(__file__).resolve().parents[3]
HELPERS = PROJECT / 'runs/archive_family_campaign_20261116'
RESOURCES = PROJECT / 'runs/speed_all_methods_A_20261117'
sys.path.insert(0, str(HELPERS))
from request_save_and_stop import issue, validate_request
from graceful_stop import verify_saved  # Never call pause_children or signal_save_stop.
sys.path.insert(0, str(RESOURCES))
from resource_guard import available, cgroup_headroom

GIB = 1024 ** 3
ALLOWED = {f'i{i:02d}' for i in range(2, 13)}
SAFE = re.compile(r'[A-Za-z0-9_-]{1,64}\Z')
HEX = re.compile(r'[0-9a-f]{64}\Z')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 ** 2), b''):
            h.update(block)
    return h.hexdigest()


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp-' + uuid.uuid4().hex)
    with temporary.open('x') as f:
        json.dump(value, f, indent=2)
        f.write('\n')
        f.flush()
        os.fsync(f.fileno())
    os.replace(temporary, path)


def host():
    value = socket.gethostname().split('.')[0]
    return f'i{int(value[1:]):02d}' if re.fullmatch(r'i\d+', value) else value


def identity(pid):
    proc = Path('/proc') / str(pid)
    try:
        fields = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
        return {'pid': int(pid), 'starttick': int(fields[19]), 'uid': proc.stat().st_uid, 'state': fields[0]}
    except (FileNotFoundError, ProcessLookupError):
        return None


def alive(child):
    value = identity(child['pid'])
    return value is not None and value['starttick'] == child['starttick'] and value['uid'] == os.getuid() and value['state'] != 'Z'


def runtime_paths():
    return [Path(__file__).resolve(), HELPERS / 'request_save_and_stop.py',
            HELPERS / 'graceful_stop.py', RESOURCES / 'resource_guard.py']


def validate(m):
    if m.get('schema_version') != 1:
        raise ValueError('manifest schema_version1 required')
    tasks = m['tasks']
    if len(tasks) != 11 or len({t['id'] for t in tasks}) != 11 or len({t['host'] for t in tasks}) != 11:
        raise ValueError('exactly11 uniquely assigned host/task pairs required')
    expected = {'A': {0, 1, 2}, 'B': {0, 1, 2}, 'Bprime': {0, 1, 2}, 'Bdoubleprime': {0}, 'D': {0}}
    if {method: {t['seed'] for t in tasks if t['method'] == method} for method in expected} != expected:
        raise ValueError('method/seed allocation mismatch')
    for t in tasks:
        if not SAFE.fullmatch(t['id']) or t['host'] not in ALLOWED or t['method'] not in expected:
            raise ValueError('unsafe task or out-of-policy host')
        if type(t['seed']) is not int or t['precision'] != ('float64' if t['method'] == 'D' else 'float32'):
            raise ValueError('seed/precision mismatch')
        if t['feature_cache_mib'] not in (16, 32):
            raise ValueError('cache must be16/32MiB')
        if t['threads'] != 15 or t['target_steps'] != 1000000000000 or t['first_checkpoint_step'] != 50000000000 or t['checkpoint_interval'] != 50000000000:
            raise ValueError('fresh15worker/1T/50B schedule required')
        if t['deadline_epoch'] is not None and (not isinstance(t['deadline_epoch'], (int, float)) or not math.isfinite(t['deadline_epoch'])):
            raise ValueError('deadline must be null or finite epoch')
        if Path(t['local_control_dir']) != Path('/tmp') / m['campaign_id'] / t['id']:
            raise ValueError('unexpected local control directory')
    if not Path(m['checkpoint_io_lock']).is_absolute():
        raise ValueError('absolute shared lock required')
    archive = m.get('archive_manifest')
    digest = m.get('archive_manifest_sha256')
    if archive or digest:
        if not archive or not Path(archive).is_absolute() or not isinstance(digest, str) or not HEX.fullmatch(digest):
            raise ValueError('archive manifest requires an absolute path and SHA256')
    return tasks


def verify_pins(m):
    for p in runtime_paths():
        if m['runtime_hashes'].get(str(p)) != sha(p):
            raise RuntimeError('runtime hash mismatch: ' + str(p))
    checked = set()
    for release in m['tasks']:
        key = tuple(release[k] for k in ('binary', 'binary_sha256', 'source_dir', 'source_manifest', 'source_sha256'))
        if key in checked:
            continue
        checked.add(key)
        for field in ('source_sha256', 'binary_sha256'):
            if not HEX.fullmatch(release[field]):
                raise ValueError('invalid release digest')
        if sha(release['binary']) != release['binary_sha256']:
            raise RuntimeError('binary hash mismatch')
        manifest = Path(release['source_manifest'])
        text = manifest.read_text()
        if hashlib.sha256(text.encode()).hexdigest() != release['source_sha256']:
            raise RuntimeError('source manifest hash mismatch')
        source = Path(release['source_dir']).resolve()
        for line in text.splitlines():
            expected, relative = line.split('  ', 1)
            path = (source / relative).resolve()
            if source not in path.parents or sha(path) != expected:
                raise RuntimeError('source pin mismatch: ' + relative)
    if m.get('archive_manifest') and sha(m['archive_manifest']) != m['archive_manifest_sha256']:
        raise RuntimeError('archive manifest changed')


def command(root, m, t):
    method = t['method']
    if Path(t['output_dir']).resolve() != root / 'runs' / t['id']:
        raise ValueError('output must be the assigned campaign run')
    argv = ['taskset', '-c', '0-14', t['binary'], str(t['seed']),
            '--experiment-id', t['id'], '--output-dir', t['output_dir'],
            '--threads', '15', '--steps', '1000000000000', '--first-checkpoint-step', '50000000000',
            '--checkpoint-interval', '50000000000', '--retention', 'no',
            '--behavior', 'tc-ucb' if method in ('A', 'D') else 'greedy',
            '--formula', 'source' if method == 'D' else 'paper', '--ucb-c', '0.0065',
            '--warmup-steps', '0', '--decay-end-steps', '0', '--model-access', 'async',
            '--checkpoint-format', 'compact', '--feature-cache-mib', str(t['feature_cache_mib']),
            '--summary-interval', '1000000', '--sample-every', str(t['sample_every']),
            '--checkpoint-io-lock', m['checkpoint_io_lock'], '--checkpoint-io-timeout-seconds', str(m['checkpoint_io_timeout_seconds']),
            '--save-and-stop-file', str(Path(t['local_control_dir']) / 'SAVE_AND_STOP.request')]
    if method in ('A', 'D'):
        argv += ['--start-pool-per-million', '0']
    else:
        argv += ['--archive-condition', method, '--archive-mode', 'random128',
                 '--start-pool-per-million', '500000', '--archive-promote32-percent', '20', '--archive-promote64-percent', '50']
        if m.get('archive_manifest'):
            argv += ['--start-pool-manifest', m['archive_manifest'],
                     '--start-pool-manifest-sha256', m['archive_manifest_sha256']]
    return argv


def swapout():
    info = dict(line.split() for line in Path('/proc/vmstat').read_text().splitlines())
    return int(info['pswpout']) * os.sysconf('SC_PAGE_SIZE')


def cpu_snapshot():
    values = {}
    for line in Path('/proc/stat').read_text().splitlines():
        fields = line.split()
        if fields and re.fullmatch(r'cpu(?:[0-9]|1[0-4])', fields[0]):
            numbers = list(map(int, fields[1:9]))
            values[fields[0]] = (sum(numbers), numbers[3] + numbers[4])
    return values


def admission(root, m, t):
    before = cpu_snapshot()
    time.sleep(1)
    after = cpu_snapshot()
    busy = {k: 1 - (after[k][1] - before[k][1]) / max(1, after[k][0] - before[k][0]) for k in before}
    free = available()
    disk = os.statvfs(root)
    disk_free = disk.f_bavail * disk.f_frsize
    needed = int(m['guards']['admission_available_gib'][t['precision']] * GIB)
    required_disk = m['guards']['admission_disk_bytes']
    reasons = []
    if len(busy) != 15:
        reasons.append('assigned CPUs unavailable')
    if free < needed:
        reasons.append('RAM/cgroup headroom insufficient')
    if disk_free < required_disk:
        reasons.append('disk insufficient for20 retained models plus reserve')
    coexisting_beatris = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        try:
            executable_path = str((entry / 'exe').resolve())
            executable = Path(executable_path).name.lower()
            if 'beatris' in executable_path.lower() and 'tta' in executable:
                coexisting_beatris.append({'pid': int(entry.name), 'executable': executable_path})
                continue
            if any(word in executable for word in ('learn_', 'playgame', 'evaluate_', 'eval_', 'bench', 'beam_search', 'tta', 'train')):
                reasons.append('existing model/evaluation process: ' + entry.name)
        except (OSError, ValueError):
            pass
    return {'reasons': reasons, 'cpu_busy': busy, 'coexisting_beatris': coexisting_beatris, 'available_bytes': free,
            'cgroup_headroom_bytes': cgroup_headroom(), 'disk_free_bytes': disk_free,
            'required_memory_bytes': needed, 'required_disk_bytes': required_disk}


def request_pause(child):
    run = Path(child['run_dir'])
    config = json.loads((run / 'config.json').read_text())
    if any(config.get(k) != child[k] for k in ('source_sha256', 'binary_sha256')):
        raise RuntimeError('save request release mismatch')
    path = Path(config['save_and_stop_file'])
    if path.exists():
        import shlex
        tokens = shlex.split(path.read_text())
        nonce = tokens[-1] if tokens else ''
        if not validate_request(path, run, child['pid'], child['starttick'], os.getuid(), child['source_sha256'], child['binary_sha256'], nonce):
            raise RuntimeError('existing request identity mismatch')
        return {'status': 'request_exists', 'path': str(path)}
    return issue(run, child['pid'], child['starttick'])


def save_until_exit(child, report):
    def progress(value):
        try:
            report(value)
        except OSError as e:
            print('save-stop diagnostic IO failed: ' + str(e), file=sys.stderr, flush=True)
    while alive(child):
        try:
            value = request_pause(child)
            progress({'status': 'save_pending', 'request': value})
        except (OSError, ValueError, RuntimeError) as e:
            progress({'status': 'save_pending_request_error', 'error': str(e)})
        time.sleep(2)
    try:
        progress({'status': 'verifying_saved_exit'})
        return verify_saved(child)
    except (OSError, ValueError, RuntimeError) as e:
        return {'status': 'exited_without_verified_save', 'error': str(e)}


def watchdog(root, readfd, ackfd, timeout):
    child = None
    normal = False
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
    def interrupted(signum, frame):
        raise InterruptedError('watchdog interrupted')
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, interrupted)
    try:
        with os.fdopen(readfd, 'rb', buffering=0) as stream, os.fdopen(ackfd, 'wb', buffering=0) as ack:
            while True:
                ready, _, _ = select.select([stream], [], [], timeout)
                if not ready:
                    break
                line = stream.readline()
                if not line:
                    break
                value = json.loads(line)
                if value.get('verification'):
                    # A completed learner is safe while its large model is
                    # independently hashed. EOF still detects controller loss.
                    timeout = 86400
                    ack.write(b'ACK\n')
                    continue
                if value.get('heartbeat'):
                    ack.write(b'ACK\n')
                    continue
                if value.get('done'):
                    normal = True
                    ack.write(b'ACK\n')
                    break
                child = value
                run = Path(child['run_dir']).resolve()
                if run.parent != root / 'runs' or run.name != child['task'] or not alive(child):
                    raise RuntimeError('watchdog registration identity mismatch')
                ack.write(b'ACK\n')
    finally:
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            signal.signal(sig, signal.SIG_IGN)
        if child and not normal:
            status = root / 'claims' / child['task'] / 'status.json'
            result = save_until_exit(child, lambda row: write(status, dict(row, recovered_by_watchdog=True, epoch=time.time())))
            write(status, {'status': 'paused' if result.get('status') == 'saved_and_exited' else 'failed',
                           'recovered_by_watchdog': True, 'saved_checkpoint': result, 'epoch': time.time()})


def run(root, m, t):
    claim = root / 'claims' / t['id']
    state = claim / 'status.json'
    if claim.exists() or (root / 'runs' / t['id']).exists():
        raise RuntimeError('fresh task already claimed or output exists')
    local = Path(t['local_control_dir'])
    if local.exists():
        raise RuntimeError('local control directory already exists; inspect before retry')
    claim.mkdir(parents=True)
    local.mkdir(parents=True)
    pause_file = root / 'control_requests' / (t['id'] + '.pause.json')
    stop = {'requested': False}
    def interrupted(signum, frame):
        stop.update(requested=True, signal=signum)
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, interrupted)
    if signal.getsignal(signal.SIGHUP) != signal.SIG_IGN:
        signal.signal(signal.SIGHUP, interrupted)
    while True:
        if pause_file.exists() or stop['requested'] or (t['deadline_epoch'] is not None and time.time() >= t['deadline_epoch']):
            write(state, {'status': 'deferred', 'reason': 'pause before launch', 'epoch': time.time()})
            return
        observation = admission(root, m, t)
        if not observation['reasons']:
            break
        write(state, {'status': 'waiting', 'host': host(), 'admission': observation, 'epoch': time.time()})
        time.sleep(m['guards']['poll_seconds'])
    verify_pins(m)
    argv = command(root, m, t)
    logs = root / 'logs'
    logs.mkdir(exist_ok=True)
    readfd, writefd = os.pipe()
    ackread, ackwrite = os.pipe()
    monitor = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--root', str(root), '--watchdog', str(readfd), str(ackwrite), '--heartbeat-timeout', str(m['guards']['heartbeat_timeout_seconds'])],
                               pass_fds=(readfd, ackwrite), start_new_session=True, stdin=subprocess.DEVNULL)
    os.close(readfd)
    os.close(ackwrite)
    child = None
    normal = False
    def send(value):
        os.write(writefd, (json.dumps(value) + '\n').encode())
        ready, _, _ = select.select([ackread], [], [], 10)
        if not ready or os.read(ackread, 4) != b'ACK\n':
            raise RuntimeError('watchdog ACK unavailable')
    def persist(value):
        value = dict(value, host=host(), epoch=time.time(), child=child)
        write(local / 'status.json', value)
        try:
            write(state, value)
        except OSError as e:
            print('shared status IO failed: ' + str(e), file=sys.stderr, flush=True)
    try:
        with (logs / (t['id'] + '.log')).open('x') as log:
            process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--gate', *argv],
                                       stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
                                       env=dict(os.environ, NT9_THP='1'))
            child = dict(identity(process.pid), task=t['id'], run_dir=t['output_dir'],
                         source_sha256=t['source_sha256'], binary_sha256=t['binary_sha256'])
            send(child)
            persist({'status': 'starting', 'command': argv, 'watchdog_pid': monitor.pid})
            process.stdin.write(b'GO\n')
            process.stdin.flush()
            process.stdin.close()
            history = deque()
            baseline_swap = swapout()
            started = time.monotonic()
            with (logs / (t['id'] + '.resources.jsonl')).open('x') as resource_log:
                while process.poll() is None:
                    now = time.time()
                    elapsed = time.monotonic() - started
                    memory_free = available()
                    disk = os.statvfs(root)
                    observation = {'epoch': now, 'elapsed': elapsed, 'available_bytes': memory_free,
                                   'cgroup_headroom_bytes': cgroup_headroom(), 'disk_free_bytes': disk.f_bavail * disk.f_frsize,
                                   'host_swapout_delta': swapout() - baseline_swap}
                    history.append((time.monotonic(), observation['host_swapout_delta']))
                    window = m['guards']['sustained_swap_window_seconds']
                    while len(history) > 1 and history[1][0] < time.monotonic() - window:
                        history.popleft()
                    # A short global swap burst is diagnostic only. Stop through
                    # the request file only for persistent high-rate swapping.
                    sustained = False
                    if len(history) > 1 and history[-1][0] - history[0][0] >= window:
                        boundaries = [history[-1][0] - window * fraction / 3 for fraction in (3, 2, 1, 0)]
                        marks = [next((x for x in reversed(history) if x[0] <= boundary), None) for boundary in boundaries]
                        if all(mark is not None for mark in marks):
                            sustained = (marks[-1][1] - marks[0][1] > m['guards']['sustained_swap_mib'] * 1024 ** 2
                                         and all(b[1] > a[1] for a, b in zip(marks, marks[1:])))
                    observation['sustained_swap_5min'] = sustained
                    observation['swap_warning_only'] = observation['host_swapout_delta'] > m['guards']['swap_warning_mib'] * 1024 ** 2
                    resource_log.write(json.dumps(observation) + '\n')
                    resource_log.flush()
                    reason = ('operator_pause' if pause_file.exists() or stop['requested'] else
                              'deadline' if t['deadline_epoch'] is not None and now >= t['deadline_epoch'] else
                              'memory_headroom' if memory_free < m['guards']['minimum_available_gib'] * GIB else
                              'disk_headroom' if observation['disk_free_bytes'] < m['guards']['minimum_disk_gib'] * GIB else
                              'sustained_swap' if sustained else None)
                    if reason:
                        persist({'status': 'save_pending', 'reason': reason, 'observation': observation})
                        def pause_progress(row):
                            persist(dict(row, reason=reason))
                            send({'verification': True} if row['status'] == 'verifying_saved_exit' else {'heartbeat': True})
                        saved = save_until_exit(child, pause_progress)
                        process.wait()
                        if saved['status'] != 'saved_and_exited':
                            raise RuntimeError('learner exited without a verified full save')
                        persist({'status': 'completed' if saved['marker'].endswith('RUN_COMPLETE.json') else 'paused', 'saved_checkpoint': saved, 'reason': reason})
                        normal = True
                        break
                    persist({'status': 'running', 'observation': observation})
                    send({'heartbeat': True})
                    time.sleep(m['guards']['poll_seconds'])
                else:
                    if process.returncode:
                        raise RuntimeError('learner failed with exit ' + str(process.returncode))
                    send({'verification': True})
                    saved = verify_saved(child)
                    persist({'status': 'completed' if saved['marker'].endswith('RUN_COMPLETE.json') else 'paused', 'saved_checkpoint': saved})
                    normal = True
    except BaseException as error:
        if child and 'process' in locals() and process.stdin and not process.stdin.closed:
            process.stdin.close()  # A never-released gate exits without starting learning.
        if child and alive(child):
            persist({'status': 'save_pending', 'controller_error': repr(error)})
            def cleanup_progress(row):
                persist(dict(row, controller_error=repr(error)))
                try:
                    send({'verification': True} if row['status'] == 'verifying_saved_exit' else {'heartbeat': True})
                except (OSError, RuntimeError):
                    pass
            saved = save_until_exit(child, cleanup_progress)
            persist({'status': 'paused' if saved['status'] == 'saved_and_exited' else 'failed', 'saved_checkpoint': saved, 'controller_error': repr(error)})
            normal = saved['status'] == 'saved_and_exited'
        elif child:
            try:
                saved = verify_saved(child)
                persist({'status': 'paused' if saved['marker'].endswith('RUN_PAUSED.json') else 'completed', 'saved_checkpoint': saved, 'controller_error': repr(error)})
                normal = True
            except (OSError, ValueError, RuntimeError):
                persist({'status': 'failed', 'controller_error': repr(error)})
        raise
    finally:
        try:
            if normal:
                try:
                    send({'done': True})
                except (OSError, RuntimeError) as e:
                    print('saved learner; watchdog completion ACK unavailable: ' + str(e), file=sys.stderr, flush=True)
        finally:
            os.close(writefd)
            os.close(ackread)
        if normal:
            try:
                monitor.wait(timeout=20)
            except subprocess.TimeoutExpired:
                print('watchdog continues independent saved-checkpoint audit', file=sys.stderr, flush=True)


def main():
    if '--gate' in sys.argv:
        if host() not in ALLOWED:
            raise RuntimeError('gate host outside i02..i12')
        index = sys.argv.index('--gate')
        if sys.stdin.readline() != 'GO\n':
            raise SystemExit(3)
        os.execvp(sys.argv[index + 1], sys.argv[index + 1:])
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--task')
    parser.add_argument('--manifest-sha256')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--watchdog', nargs=2, type=int)
    parser.add_argument('--heartbeat-timeout', type=float, default=180)
    args = parser.parse_args()
    root = args.root.resolve()
    if host() not in ALLOWED:
        raise RuntimeError('host outside i02..i12')
    if args.watchdog:
        watchdog(root, *args.watchdog, args.heartbeat_timeout)
        return
    if sha(root / 'manifest.json') != args.manifest_sha256:
        raise RuntimeError('manifest pin mismatch')
    m = json.loads((root / 'manifest.json').read_text())
    tasks = validate(m)
    t = next(t for t in tasks if t['id'] == args.task)
    if t['host'] != host():
        raise RuntimeError('task assigned to a different host')
    verify_pins(m)
    if not args.apply:
        print(json.dumps({'status': 'dry_run', 'task': t, 'command': command(root, m, t)}, indent=2))
        return
    with open('/tmp/ab_family_1t_' + host() + '.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        run(root, m, t)


if __name__ == '__main__':
    main()
