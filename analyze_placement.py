import glob
import re
import numpy as np
import matplotlib.pyplot as plt
from concurrent.futures import ProcessPoolExecutor


# =========================
# top3 tile
# =========================

def get_top3(board):

    a = b = c = -1
    ai = bi = ci = 0

    for i, x in enumerate(board):

        if x > a:
            c, ci = b, bi
            b, bi = a, ai
            a, ai = x, i

        elif x > b:
            c, ci = b, bi
            b, bi = x, i

        elif x > c:
            c, ci = x, i

    return ai, bi, ci



# =========================
# placement
# =========================

def classify(board):

    i1,i2,i3 = get_top3(board)

    r1,c1 = divmod(i1,4)
    r2,c2 = divmod(i2,4)
    r3,c3 = divmod(i3,4)


    straight = (
        r1==r2==r3
        or
        c1==c2==c3
    )


    rmin=min(r1,r2,r3)
    rmax=max(r1,r2,r3)

    cmin=min(c1,c2,c3)
    cmax=max(c1,c2,c3)


    corner = (
        (rmax-rmin<=1 and cmax-cmin<=2)
        or
        (rmax-rmin<=2 and cmax-cmin<=1)
    )


    return straight, corner



# =========================
# analyze
# =========================

def analyze_file(path):

    result=[]

    prev_game=-1
    prev_board=None


    with open(path) as f:

        it=iter(f)

        for line in it:

            if not line.startswith("game,"):
                continue


            m=re.search(
                r"game,(\d+),turn,(\d+)",
                line
            )

            if m is None:
                continue


            game=int(m.group(1))


            try:
                board_line=next(it)

            except StopIteration:
                break


            if not board_line.startswith("board,"):
                continue


            board_line=board_line.replace(
                "board,",
                ""
            )


            try:
                board=tuple(
                    map(
                        int,
                        board_line.split(",")
                    )
                )

            except:
                continue


            if len(board)!=16:
                continue


            # 到達していないターン除外
            if (
                game==prev_game
                and board==prev_board
            ):
                continue


            prev_game=game
            prev_board=board


            s,c=classify(board)


            result.append(
                (game,int(s),int(c))
            )


    return result



# =========================
# moving average
# =========================

def moving_average(x, window=100):

    x=np.array(x)

    return np.convolve(
        x,
        np.ones(window)/window,
        mode="valid"
    )



# =========================
# main
# =========================

if __name__=="__main__":


    tuple_data={}


    for t in range(6,10):

        files=glob.glob(
            f"learn_double/logs/{t}tuple_seed*.log"
        )


        print(
            f"{t}tuple {len(files)} files"
        )


        with ProcessPoolExecutor(
            max_workers=8
        ) as executor:

            results=list(
                executor.map(
                    analyze_file,
                    files
                )
            )


        data=[]


        for r in results:
            data.extend(r)


        # ゲーム順
        data.sort(
            key=lambda x:x[0]
        )


        tuple_data[t]=data



    # =====================
    # plot
    # =====================

    plt.figure(
        figsize=(10,6)
    )


    for t,data in tuple_data.items():


        straight=[
            x[1]
            for x in data
        ]


        ma=moving_average(
            straight,
            100
        )


        plt.plot(
            ma*100,
            label=f"{t}tuple"
        )


    plt.xlabel(
        "Game (100 game moving average)"
    )


    plt.ylabel(
        "Straight ratio (%)"
    )


    plt.title(
        "Top3 tile straight alignment"
    )


    plt.grid()

    plt.legend()


    plt.tight_layout()


    plt.savefig(
        "straight_ratio_ma100.png",
        dpi=300
    )


    plt.close()


    print(
        "saved straight_ratio_ma100.png"
    )