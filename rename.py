import os
import re

base_dir = "board_data"

for name in os.listdir(base_dir):
    old_path = os.path.join(base_dir, name)

    # ディレクトリのみ対象
    if not os.path.isdir(old_path):
        continue

    m = re.match(r"tuples(\d+)-seed(\d+)-VSE-count(\d+)", name)

    if not m:
        continue

    nt = m.group(1)
    seed = m.group(2)
    count = m.group(3)

    # count の4桁目を EXP に使用
    exp = count[3] if len(count) >= 4 else "0"

    # 必要に応じて変更
    tn = nt
    oi = "0"

    new_name = f"EXP_{exp}-NT{nt}-TN{tn}-OI{oi}-seed{seed}"
    new_path = os.path.join(base_dir, new_name)

    print(f"{name} -> {new_name}")

    os.rename(old_path, new_path)