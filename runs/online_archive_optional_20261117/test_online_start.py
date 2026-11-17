"""Small-table regression: optional online bootstrap and strict external inputs."""
from pathlib import Path
import json
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
BINARY = HERE / 'build/bin/learn3_ttas'


def main():
    with tempfile.TemporaryDirectory(prefix='online-start-') as temporary:
        root = Path(temporary)
        common = [str(BINARY), '7', '--threads', '1', '--steps', '1000',
                  '--first-checkpoint-step', '1000', '--checkpoint-interval', '50000',
                  '--checkpoint-format', 'compact', '--checkpoint-io-lock', str(root / 'io.lock'),
                  '--archive-mode', 'random128']
        for method in ('B', 'Bprime', 'Bdoubleprime', 'C'):
            output = root / method
            settings = (['--archive-condition', method] if method != 'C' else
                        ['--behavior', 'tc-ucb', '--formula', 'source',
                         '--start-pool-per-million', '500000', '--archive-source-update', '1'])
            result = subprocess.run(common + settings + ['--output-dir', str(output)],
                                    capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, (method, result.stderr, result.stdout[-2000:])
            config = json.loads((output / 'config.json').read_text())
            assert config['start_pool_manifest'] == '' and config['start_pool_path'] == ''
            assert config['archive_mode'] == 'random128'
            assert list(output.rglob('COMPLETE')), method
        for index, settings in enumerate((
            ['--archive-mode', 'legacy'],
            ['--start-pool-manifest-sha256', 'a' * 64],
            ['--start-pool-manifest', str(root / 'missing')],
            ['--start-pool-manifest', str(root / 'missing'), '--start-pool-manifest-sha256', 'a' * 64],
        )):
            output = root / f'invalid{index}'
            result = subprocess.run(common + ['--archive-condition', 'B'] + settings +
                                    ['--output-dir', str(output)], capture_output=True, text=True, timeout=10)
            assert result.returncode != 0 and not output.exists(), (settings, result.stdout)
    print('PASS: four online methods start without external data; legacy and invalid inputs fail')


if __name__ == '__main__':
    main()
