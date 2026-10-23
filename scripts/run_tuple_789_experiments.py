#!/usr/bin/env python3
"""Calibrate each tuple, then play greedy + eight beam policies, ten seeds each."""
import argparse
import csv
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
JST = dt.timezone(dt.timedelta(hours=9))
POLICIES = ('fixed', 'sqrt', 'sqrt_budget', 'cbrt', 'power', 'increasing', 'decreasing', 'adaptive')
MODELS = ((7, 2), (8, 9), (9, 2))


def now():
    return dt.datetime.now(JST).isoformat(timespec='seconds')


def read_csv(path):
    with path.open() as file:
        return list(csv.DictReader(file))


def write_csv(path, fields, rows):
    with path.open('w') as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def summarize(directory, tuple_type, training_seed, depths, count):
    rows = read_csv(directory / 'game_scores.csv')
    summaries = []
    for method in ('greedy', *POLICIES):
        selected = [r for r in rows if r['method'] == method]
        good = [r for r in selected if r['status'] == 'terminal']
        if any(int(r['tuple_type']) != tuple_type for r in selected):
            raise RuntimeError('Wrong tuple evaluator in game output')
        starts = [int(r['started_unix_us']) for r in selected]
        ends = [int(r['finished_unix_us']) for r in selected]
        n = len(good)
        summaries.append(dict(
            tuple_type=tuple_type, training_seed=training_seed, method=method,
            depth=depths[method], requested_games=count, completed_games=n,
            failed_games=len(selected)-n,
            mean_score=sum(int(r['score']) for r in good)/n if n else '',
            mean_max_tile=sum(int(r['max_tile']) for r in good)/n if n else '',
            mean_moves=sum(int(r['moves']) for r in good)/n if n else '',
            total_search_seconds=sum(int(r['search_time_us']) for r in selected)/1e6,
            total_game_wall_seconds=sum(int(r['wall_time_us']) for r in selected)/1e6,
            method_elapsed_seconds=(max(ends)-min(starts))/1e6 if selected else '',
            method_started_at=dt.datetime.fromtimestamp(min(starts)/1e6,JST).isoformat() if starts else '',
            method_finished_at=dt.datetime.fromtimestamp(max(ends)/1e6,JST).isoformat() if ends else '',
        ))
    write_csv(directory / 'summary.csv', summaries[0].keys(), summaries)
    return summaries


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, default=ROOT/'learn_double/count004_2e11')
    parser.add_argument('--threads', type=int, default=16)
    parser.add_argument('--calibration-threads', type=int, default=8)
    parser.add_argument('--max-depth', type=int, default=512)
    parser.add_argument('--calibration-moves', type=int, default=16)
    parser.add_argument('--games', type=int, default=10)
    args = parser.parse_args()
    if not (1 <= args.threads <= 32 and 1 <= args.calibration_threads <= 8 and
            1 <= args.max_depth <= 512 and args.games > 0 and args.calibration_moves > 0):
        parser.error('Invalid concurrency, depth or game counts')
    base = args.run_dir.resolve()
    base.mkdir(parents=True, exist_ok=True)
    (base/'PID').write_text(str(os.getpid())+'\n')
    settings = vars(args).copy()
    settings.update(models=MODELS, target_ms=1000, width_budget=100000, max_width=2048,
                    methods=['greedy',*POLICIES], started_at=now())
    (base/'parameters.json').write_text(json.dumps(settings,default=str,indent=2)+'\n')
    start = time.monotonic()
    failed = False
    all_summaries = []
    phase_file = (base/'phase_times.csv').open('w', buffering=1)
    phases = csv.writer(phase_file)
    phases.writerow(['tuple_type','training_seed','phase','started_at','finished_at','elapsed_seconds','exit_code'])

    def run_phase(command, directory, tuple_type, seed, phase):
        stamp = now()
        (base/'STATUS').write_text(f'RUNNING tuple={tuple_type} seed={seed} phase={phase}\n')
        (directory/'STATUS').write_text(f'RUNNING phase={phase}\n')
        print(f'STARTED tuple={tuple_type} seed={seed} phase={phase}',flush=True)
        tick = time.monotonic()
        with (directory/f'{phase}.log').open('w') as log:
            child = subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
            (directory/f'{phase}.PID').write_text(str(child.pid)+'\n')
            code = child.wait()
        phases.writerow([tuple_type,seed,phase,stamp,now(),time.monotonic()-tick,code])
        phase_file.flush()
        return code

    try:
        for tuple_type,seed in MODELS:
            directory = base/f'tuple{tuple_type}_seed{seed}'
            directory.mkdir(exist_ok=True)
            tick = time.monotonic()
            started_at = now()
            binary = ROOT/f'testplay_kmatsu/test_beam_search_afterstate_{tuple_type}'
            data = args.data_dir/f'tuples{tuple_type}-seed{seed}-VSE-count004.dat'
            if not data.is_file() or not binary.is_file():
                raise RuntimeError(f'Missing executable or data for tuple {tuple_type}: {data}')
            common = ['--timeout-sec','60','--k','64','--alpha','0.4','--w0','64',
                      '--ratio','2','--sigma','1','--budget','100000']
            calibration = directory/'calibration'
            command = [str(binary),'0','1','1','fixed','2048',str(data),*common,
                       '--threads',str(args.calibration_threads),'--max-moves',str(args.calibration_moves),
                       '--calibrate-dir',str(calibration),'--max-depth',str(args.max_depth),'--target-ms','1000']
            if run_phase(command,directory,tuple_type,seed,'calibration'):
                raise RuntimeError(f'Calibration failed for tuple {tuple_type}; see calibration.log')
            selection = [read_csv(calibration/m/'selection.csv')[0] for m in POLICIES]
            if any(int(r['selected_depth']) < 1 for r in selection):
                raise RuntimeError('No depth below target for one or more policies')
            write_csv(directory/'selected_depths.csv',selection[0].keys(),selection)
            depths = {'greedy':1,**{r['method']:int(r['selected_depth']) for r in selection}}
            games = directory/'games'
            games.mkdir(exist_ok=True)
            manifest = directory/'jobs.tsv'
            with manifest.open('w') as file:
                for method,depth in depths.items():
                    for game_seed in range(args.games):
                        file.write(f'{game_seed}\t{depth}\t{method}\t{games/f"{method}_seed{game_seed}.csv"}\n')
            command = [str(binary),'0','1','1','fixed','2048',str(data),*common,
                       '--threads',str(args.threads),'--max-moves','0','--jobs-file',str(manifest),
                       '--scores-file',str(directory/'game_scores.csv')]
            code = run_phase(command,directory,tuple_type,seed,'games')
            summaries = summarize(directory,tuple_type,seed,depths,args.games)
            okay = code == 0 and all(r['completed_games']==args.games for r in summaries)
            failed |= not okay
            all_summaries.extend(summaries)
            write_csv(base/'summary.csv',all_summaries[0].keys(),all_summaries)
            (directory/'timing.json').write_text(json.dumps(dict(started_at=started_at,
                finished_at=now(),total_wall_seconds=time.monotonic()-tick),indent=2)+'\n')
            (directory/'STATUS').write_text('COMPLETED\n' if okay else 'FAILED see game_scores.csv\n')
        (base/'STATUS').write_text('FAILED see per-tuple outputs\n' if failed else 'COMPLETED\n')
    except Exception as error:
        (base/'STATUS').write_text(f'FAILED {error}\n')
        raise
    finally:
        phase_file.close()
        (base/'timing.json').write_text(json.dumps(dict(started_at=settings['started_at'],
            finished_at=now(),total_wall_seconds=time.monotonic()-start),indent=2)+'\n')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
