from array import array
import os

base = "/home2/matsu-lab2/terauchi/2048_NewTypeTuple/board_data"

for nt in [6, 7, 8, 9]:

    path = f"{base}/EXP_1-NT{nt}-TN{nt}-OI0-seed0/after_state.txt"

    print(f"\n===== NT{nt} =====")

    reach = [array('i') for _ in range(32)]
    seen = [False] * 32

    turn = 0

    if not os.path.exists(path):
        print(f"not found: {path}")
        continue

    with open(path, buffering=1024*1024) as f:
        for line in f:

            if line[0] == 'g':
                turn = 0
                seen = [False] * 32
                continue

            turn += 1

            for x in line.split():
                exp = int(x)

                if exp and not seen[exp]:
                    seen[exp] = True
                    reach[exp].append(turn)


    for exp, data in enumerate(reach):
        if not data:
            continue

        print(
            f"2^{exp:2d} ({1<<exp:5d}) : "
            f"mean={sum(data)/len(data):7.1f}, "
            f"min={min(data):4d}, "
            f"max={max(data):4d}, "
            f"count={len(data)}"
        )