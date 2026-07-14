import os
import re
import json
import glob
from concurrent.futures import ThreadPoolExecutor

def parse_directory_name(dir_name):
    params = {
        'exp': 1,
        'nt': None,
        'tn': None,
        'oi': None,
        'seed': None
    }

    patterns = {
        'exp': r'EXP_(\d+)',
        'nt': r'NT(\d+)',
        'tn': r'TN(\d+)',
        'oi': r'OI(\d+)',
        'seed': r'seed(\d+)'
    }

    for key, pattern in patterns.items():
        match = re.search(pattern, dir_name)
        if match:
            params[key] = int(match.group(1))

    return params


def parse_after_state(file_path):
    scores = []
    gameover_turns = []

    score_pattern = re.compile(r'score:(\d+)')
    gameover_pattern = re.compile(r'gameover_turn:(\d+)')

    with open(file_path, 'r') as f:
        for line in f:
            m = score_pattern.search(line)
            if m:
                scores.append(int(m.group(1)))

            m = gameover_pattern.search(line)
            if m:
                gameover_turns.append(int(m.group(1)))

    return scores, gameover_turns


def process_directory(exp_dir):
    params = parse_directory_name(exp_dir)

    after_state_path = os.path.join(exp_dir, 'after_state.txt')

    if not os.path.exists(after_state_path):
        return None

    scores, gameover_turns = parse_after_state(after_state_path)

    if not scores or not gameover_turns:
        return None

    mean_score = sum(scores) / len(scores)
    mean_turn = sum(gameover_turns) / len(gameover_turns)

    result = {
        'exp': params['exp'],
        'nt': params['nt'],
        'tn': params['tn'],
        'oi': params['oi'],
        'seed': params['seed'],
        'mean_score': mean_score,
        'mean_turn': mean_turn,
        'count': len(scores)
    }

    return result


def create_analysis_json(pattern):
    base_dir = 'board_data'
    full_pattern = os.path.join(base_dir, pattern)

    dirs = [
        d for d in glob.glob(full_pattern)
        if os.path.isdir(d)
    ]

    with ThreadPoolExecutor(max_workers=os.cpu_count()) as executor:
        results = list(executor.map(process_directory, dirs))

    results = [r for r in results if r is not None]

    with open('analysis_results.json', 'w') as f:
        json.dump(results, f, indent=2)

    print(f"合計 {len(results)} 件")