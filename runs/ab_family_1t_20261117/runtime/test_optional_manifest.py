"""Controller-only checks; no learners or network connections are started."""
from copy import deepcopy
from pathlib import Path
import json
import tempfile
from unittest.mock import patch

import controller


def main():
    root = Path(controller.__file__).resolve().parents[1]
    original = {'schema_version': 1, 'campaign_id': 'test_optional_manifest',
                'checkpoint_io_lock': '/tmp/test_optional_manifest.lock',
                'checkpoint_io_timeout_seconds': 1200,
                'archive_manifest': '/tmp/test_optional_manifest.txt',
                'archive_manifest_sha256': 'a' * 64, 'tasks': []}
    for method, seeds in {'A': (0, 1, 2), 'B': (0, 1, 2), 'Bprime': (0, 1, 2),
                          'Bdoubleprime': (0,), 'D': (0,)}.items():
        for seed in seeds:
            task_id = f'{method}_{seed}'
            original['tasks'].append(dict(id=task_id, host=f'i{len(original["tasks"]) + 2:02d}',
                method=method, seed=seed, precision='float64' if method == 'D' else 'float32',
                feature_cache_mib=16, threads=15, target_steps=1000000000000,
                first_checkpoint_step=50000000000, checkpoint_interval=50000000000,
                deadline_epoch=None, local_control_dir='/tmp/test_optional_manifest/' + task_id,
                binary='/tmp/test_binary', output_dir=str(root / 'runs' / task_id), sample_every=100000))
    without = deepcopy(original)
    without.pop('archive_manifest')
    without.pop('archive_manifest_sha256')
    controller.validate(without)
    for task in without['tasks']:
        argv = controller.command(root, without, task)
        assert '--start-pool-manifest' not in argv
        assert '--start-pool-manifest-sha256' not in argv
    controller.validate(original)
    for task in original['tasks']:
        argv = controller.command(root, original, task)
        assert ('--start-pool-manifest' in argv) == (task['method'] not in ('A', 'D'))
    for fields in ({'archive_manifest': '/tmp/pool'},
                   {'archive_manifest_sha256': 'a' * 64},
                   {'archive_manifest': 'relative', 'archive_manifest_sha256': 'a' * 64},
                   {'archive_manifest': '/tmp/pool', 'archive_manifest_sha256': 'invalid'}):
        invalid = dict(without, **fields)
        try:
            controller.validate(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError(fields)
    with patch.object(controller, 'runtime_paths', return_value=[]):
        controller.verify_pins({'tasks': []})
        with tempfile.TemporaryDirectory() as directory:
            pool = Path(directory) / 'manifest'
            pool.write_text('fixture')
            controller.verify_pins({'tasks': [], 'archive_manifest': str(pool),
                                    'archive_manifest_sha256': controller.sha(pool)})
            try:
                controller.verify_pins({'tasks': [], 'archive_manifest': str(pool),
                                        'archive_manifest_sha256': 'a' * 64})
            except RuntimeError:
                pass
            else:
                raise AssertionError('changed manifest accepted')
    print('PASS: optional manifest validation, commands and hash verification')


if __name__ == '__main__':
    main()
