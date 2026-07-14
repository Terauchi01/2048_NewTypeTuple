import json
import pandas as pd
import numpy as np

def analyze_results():

    # JSONファイル読み込み
    with open('analysis_results.json', 'r') as f:
        data = json.load(f)

    # DataFrame化
    df = pd.DataFrame(data)

    print(df.columns)
    print(df.head())

    # グループ化
    grouped = df.groupby(
        ['exp', 'nt', 'tn', 'oi', 'seed']
    ).agg({
        'mean_score': ['mean', 'std', 'count'],
        'std_score': 'mean',
    }).round(2)

    # カラム名整理
    grouped.columns = [
        'mean_score',
        'std_of_means',
        'num_seeds',
        'avg_std_score'
    ]

    print("=== パラメータごとの統計 ===")
    print(grouped)

    # CSV保存
    grouped.to_csv('analyzed_results.csv')

    print("\n結果を analyzed_results.csv に保存しました")

    return grouped

if __name__ == '__main__':
    analyze_results()