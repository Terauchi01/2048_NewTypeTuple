import contextlib
import io
import json
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import launch

def field(command, name):
    return command[command.index(name) + 1]

def main():
    launch.verify_release()
    conditions = json.loads((launch.PROJECT / 'tools/exploration_campaign/archive_conditions.json').read_text())
    assert conditions['conditions']['B']['archive_start_policy'] == 'requests'
    assert conditions['conditions']['B']['archive_source_update'] is False
    assert conditions['conditions']['Bprime']['archive_start_policy'] == 'requests'
    assert conditions['conditions']['Bprime']['archive_source_update'] is True
    assert conditions['conditions']['Bdoubleprime']['archive_start_policy'] == 'steps'
    assert conditions['conditions']['Bdoubleprime']['archive_source_update'] is False
    expected = {
        'A': ('tc-ucb', 'paper', None),
        'B': ('greedy', 'paper', 'B'),
        'Bprime': ('greedy', 'paper', 'Bprime'),
        'Bdoubleprime': ('greedy', 'paper', 'Bdoubleprime'),
        'C': ('tc-ucb', 'source', None),
    }
    results = {}
    for method, (behavior, formula, condition) in expected.items():
        args = ['--method', method, '--seed', '7', '--steps', '10000000',
                '--output-dir', str(launch.HERE / ('never_created_' + method))]
        output = io.StringIO()
        with patch.object(launch, 'verify_release'), contextlib.redirect_stdout(output):
            assert launch.main(args) == 0
        data = json.loads(output.getvalue())
        cmd = data['command']
        assert data['mode'] == 'dry-run' and not Path(field(cmd, '--output-dir')).exists()
        assert field(cmd, '--behavior') == behavior and field(cmd, '--formula') == formula
        assert field(cmd, '--threads') == '15' and field(cmd, '--model-access') == 'async'
        assert field(cmd, '--decay-end-steps') == ('0' if method in ('A', 'C') else '180000000000')
        assert field(cmd, '--ucb-c') == '0.0065'
        assert field(cmd, '--checkpoint-format') == 'compact'
        assert field(cmd, '--checkpoint-interval') == '50000000000'
        assert field(cmd, '--first-checkpoint-step') == '1000000'
        if method == 'A':
            assert field(cmd, '--start-pool-per-million') == '0'
            assert '--archive-condition' not in cmd and '--start-pool-manifest' not in cmd
        elif method == 'C':
            assert field(cmd, '--start-pool-per-million') == '500000'
            assert field(cmd, '--archive-start-policy') == 'requests'
            assert field(cmd, '--archive-source-update') == '1'
        else:
            assert field(cmd, '--archive-condition') == condition
        results[method] = 'PASS'
    invalid = [
        [],
        ['--method', 'D', '--seed', '1', '--steps', '1', '--output-dir', '/tmp/x'],
        ['--method', 'A', '--seed', '-1', '--steps', '1', '--output-dir', '/tmp/x'],
        ['--method', 'A', '--seed', '1', '--steps', '0', '--output-dir', '/tmp/x'],
        ['--method', 'A', '--seed', '1', '--steps', '1', '--output-dir', '/tmp/x', '--pool-manifest', str(launch.DEFAULT_MANIFEST)],
        ['--method', 'C', '--seed', '1', '--steps', '1', '--output-dir', str(launch.HERE)],
        ['--method', 'C', '--seed', '1', '--steps', '1', '--output-dir', '/tmp/x', '--resume-full', '/does/not/exist'],
        ['--method', 'A', '--seed', '1', '--steps', '1', '--output-dir', '/tmp/x', '--checkpoint-interval', '0'],
        ['--method', 'A', '--seed', '1', '--steps', '1', '--output-dir', '/tmp/x', '--checkpoint-io-timeout-seconds', '86401'],
        ['--method', 'A', '--seed', '1', '--steps', '1', '--output-dir', '/tmp/x', '--checkpoint-io-lock', '/does/not/exist/lock'],
    ]
    for args in invalid:
        try:
            with patch.object(launch, 'verify_release'), contextlib.redirect_stderr(io.StringIO()):
                launch.main(args)
        except SystemExit as exc:
            assert exc.code == 2
        else:
            raise AssertionError('invalid arguments accepted: ' + repr(args))
    for method in expected:
        output = io.StringIO()
        args = ['--method', method, '--seed', '7', '--steps', '1000000000000',
                '--output-dir', str(launch.HERE / ('never_created_1t_' + method)),
                '--first-checkpoint-step', '50000000000', '--checkpoint-interval', '50000000000',
                '--sample-every', '100000', '--checkpoint-io-lock', str(launch.HERE / 'shared_io.lock'),
                '--checkpoint-io-timeout-seconds', '43200', '--feature-cache-mib', '32']
        with patch.object(launch, 'verify_release'), contextlib.redirect_stdout(output):
            assert launch.main(args) == 0
        command = json.loads(output.getvalue())['command']
        assert field(command, '--steps') == '1000000000000'
        assert field(command, '--first-checkpoint-step') == '50000000000'
        assert field(command, '--checkpoint-interval') == '50000000000'
        assert field(command, '--sample-every') == '100000'
        assert field(command, '--checkpoint-io-lock') == str(launch.HERE / 'shared_io.lock')
        assert field(command, '--checkpoint-io-timeout-seconds') == '43200'
        assert field(command, '--feature-cache-mib') == '32'
        assert field(command, '--decay-end-steps') == ('0' if method in ('A', 'C') else '180000000000')
        assert field(command, '--ucb-c') == '0.0065'
        plan = json.loads(output.getvalue())['storage_plan']
        assert plan['configured_checkpoint_upper_bound'] == 20
        assert plan['estimated_model_bytes'] == 20 * launch.MODEL_CHECKPOINT_BYTES
    assert launch.scheduled_checkpoint_upper_bound(0, 1000000000000, 1000000, 50000000000) == 21
    assert launch.scheduled_checkpoint_upper_bound(0, 1000000000000, 50000000000, 120000000000) == 27
    assert launch.scheduled_checkpoint_upper_bound(50000001000, 1000000000000, 50000000000, 50000000000) == 19
    assert launch.scheduled_checkpoint_upper_bound(0, 1, 1000000, 50000000000) == 1
    low_disk_args = ['--method', 'A', '--seed', '0', '--steps', '1000000000000',
                     '--output-dir', str(launch.HERE / 'never_created_low_disk'), '--run']
    with patch.object(launch, 'verify_release'), patch.object(launch.socket, 'gethostname', return_value='i02'), \
         patch.object(launch.os, 'getloadavg', return_value=(0.0, 0.0, 0.0)), \
         patch.object(launch.os, 'sched_getaffinity', return_value=set(range(16))), \
         patch.object(launch, 'available', return_value=123 * launch.GIB), \
         patch.object(launch.shutil, 'disk_usage', return_value=SimpleNamespace(free=100 * launch.GIB)), \
         patch.object(launch.subprocess, 'Popen', side_effect=AssertionError('must not spawn')), \
         contextlib.redirect_stdout(io.StringIO()):
        try:
            launch.main(low_disk_args)
        except RuntimeError as exc:
            assert 'insufficient available memory or output disk space' in str(exc)
        else:
            raise AssertionError('1T launch accepted insufficient checkpoint space')
    allowed_hosts = ('i02', 'i2', 'i05', 'i9', 'i10', 'i11', 'i12', 'i12.example')
    denied_hosts = ('gw', 'i01', 'i1', 'i13', 'i14', 'i15', 'i012', 'localhost')
    for host in allowed_hosts:
        assert launch.is_allowed_host(host), host
    for host in denied_hosts:
        assert not launch.is_allowed_host(host), host
        args = ['--method', 'A', '--seed', '7', '--steps', '1000', '--output-dir',
                str(launch.HERE / 'never_created_host_guard'), '--run']
        with patch.object(launch, 'verify_release'), patch.object(launch.socket, 'gethostname', return_value=host), \
             patch.object(launch.subprocess, 'Popen', side_effect=AssertionError('must not spawn')), \
             contextlib.redirect_stdout(io.StringIO()):
            try:
                launch.main(args)
            except RuntimeError as exc:
                assert 'authorized i02–i12' in str(exc)
            else:
                raise AssertionError('unauthorized host accepted: ' + host)
    print(json.dumps({'status': 'PASS', 'methods': results, 'invalid_cases': len(invalid),
                      'allowed_host_cases': len(allowed_hosts), 'rejected_host_cases': len(denied_hosts),
                      'release_hash_verified': True, 'training_not_started': True}, indent=2))

if __name__ == '__main__':
    main()
