import os
import re
import json
import glob
import argparse
from pathlib import Path

def parse_directory_name(dir_name):
    """ディレクトリ名からパラメータを抽出"""
    params = {
        'exp': 1,  # デフォルト値を1に設定
        'nt': None,
        'tn': None,
        'oi': None,
        'seed': None
    }
    
    # パラメータを抽出する正規表現パターン
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
    """after-state.txtからスコアとgameover_turnを抽出"""
    scores = []
    gameover_turns = []
    patterns = {
        'score': r'score:(\d+)',
        'gameover': r'gameover_turn:(\d+)'
    }
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                for key, pattern in patterns.items():
                    match = re.search(pattern, line)
                    if match:
                        if key == 'score':
                            scores.append(int(match.group(1)))
                        elif key == 'gameover':
                            gameover_turns.append(int(match.group(1)))
    except Exception as e:
        print(f"エラー: {file_path} の読み込み中にエラーが発生しました: {e}")
    
    return scores, gameover_turns

def create_analysis_json(pattern):
    """解析結果のJSONファイルを作成"""
    results = []
    base_dir = 'board_data'
    
    # 指定されたパターンでboard_data以下のディレクトリを検索
    full_pattern = os.path.join(base_dir, pattern)
    print("\n=== 検出されたディレクトリとパラメータ ===")
    
    for exp_dir in glob.glob(full_pattern):
        if not os.path.isdir(exp_dir):
            continue
            
        # ディレクトリ名からパラメータを抽出
        params = parse_directory_name(exp_dir)
        
        # ディレクトリ名とパラメータを出力
        print(f"\nディレクトリ: {exp_dir}")
        print("パラメータ:")
        print(f"  EXP: {params['exp']}")
        print(f"  NT:  {params['nt']}")
        print(f"  TN:  {params['tn']}")
        print(f"  OI:  {params['oi']}")
        print(f"  SEED: {params['seed']}")
        
        # after-state.txtを検索
        after_state_path = os.path.join(exp_dir, 'after_state.txt')
        if os.path.exists(after_state_path):
            # スコアを抽出
            scores, gameover_turns = parse_after_state(after_state_path)
            
            if scores and gameover_turns:
                # 結果を辞書として保存
                result = {
                    'exp': params['exp'],
                    'nt': params['nt'],
                    'tn': params['tn'],
                    'oi': params['oi'],
                    'seed': params['seed'],
                    'scores': scores,
                    'gameover_turns': gameover_turns,
                    'mean_score': sum(scores) / len(scores),
                    'mean_turn': sum(gameover_turns) / len(gameover_turns),
                    'median_score': sorted(scores)[len(scores)//2],
                    'median_turn': sorted(gameover_turns)[len(gameover_turns)//2],
                    'std_score': None,  # 標準偏差は後で計算
                    'std_turn': None,   # ターンの標準偏差も後で計算
                    'count': len(scores)
                }
                
                # スコアの標準偏差を計算
                mean_score = result['mean_score']
                squared_diff_sum = sum((x - mean_score) ** 2 for x in scores)
                result['std_score'] = (squared_diff_sum / len(scores)) ** 0.5
                
                # ターンの標準偏差を計算
                mean_turn = result['mean_turn']
                squared_diff_sum = sum((x - mean_turn) ** 2 for x in gameover_turns)
                result['std_turn'] = (squared_diff_sum / len(gameover_turns)) ** 0.5
                
                results.append(result)
                print(f"処理完了: {exp_dir}")
    
    # 結果をJSONファイルに保存
    with open('analysis_results.json', 'w') as f:
        json.dump(results, f, indent=2)
    
    print(f"\n合計 {len(results)} 件のデータを analysis_results.json に保存しました")

def main():
    parser = argparse.ArgumentParser(description='実験結果を解析してJSONファイルを作成')
    parser.add_argument('pattern', 
                       help='board_data以下の検索パターン（例: EXP_* または EXP_2_NT5*）')
    args = parser.parse_args()
    
    # パターンの存在確認
    full_pattern = os.path.join('board_data', args.pattern)
    if not glob.glob(full_pattern):
        print(f"エラー: パターン '{full_pattern}' に一致するディレクトリが見つかりません")
        return
    
    create_analysis_json(args.pattern)

if __name__ == '__main__':
    main()