import glob
import os
import re
import numpy as np
import matplotlib.pyplot as plt
from concurrent.futures import ProcessPoolExecutor


WINDOW = 10000


# =========================
# top3
# =========================

def get_top3(board):

    a = b = c = -1
    ai = bi = ci = 0

    for i,x in enumerate(board):

        if x > a:
            c,ci = b,bi
            b,bi = a,ai
            a,ai = x,i

        elif x > b:
            c,ci = b,bi
            b,bi = x,i

        elif x > c:
            c,ci = x,i

    return ai,bi,ci



# =========================
# classify
# =========================

def classify(board):

    i1,i2,i3 = get_top3(board)


    r1,c1 = divmod(i1,4)
    r2,c2 = divmod(i2,4)
    r3,c3 = divmod(i3,4)


    straight = (
        r1==r2==r3 or
        c1==c2==c3
    )


    block2x2 = (
        max(r1,r2,r3)-min(r1,r2,r3)<=1
        and
        max(c1,c2,c3)-min(c1,c2,c3)<=1
    )


    return straight, block2x2



# =========================
# parse
# =========================

def analyze_file(path):

    result=[]

    prev_game=-1
    prev_board=None


    with open(path) as f:

        lines=f.readlines()


    i=0

    while i < len(lines):

        line=lines[i]


        if not line.startswith("game,"):
            i+=1
            continue


        # game,xxx,turn,xxx
        p=line.split(",")

        game=int(p[1])


        i+=1

        if i>=len(lines):
            break


        board_line=lines[i]

        if not board_line.startswith("board,"):
            continue


        board=tuple(
            map(
                int,
                board_line[6:].strip().split(",")
            )
        )


        if len(board)!=16:
            i+=1
            continue



        if (
            game==prev_game
            and
            board==prev_board
        ):
            i+=1
            continue


        prev_game=game
        prev_board=board



        s,b=classify(board)

        result.append(
            (game,s,b)
        )


        i+=1


    return result



# =========================
# moving average
# =========================

def moving_average(x,w):

    x=np.asarray(
        x,
        dtype=np.float32
    )

    if len(x)<w:
        return []


    c=np.cumsum(
        np.insert(x,0,0)
    )

    return (
        c[w:]
        -
        c[:-w]
    )/w



# =========================
# main
# =========================

if __name__=="__main__":


    tuple_data={}


    workers=os.cpu_count()


    for t in range(6,10):

        files=glob.glob(
            f"learn_double/logs/{t}tuple_seed*.log"
        )


        print(
            f"{t}tuple {len(files)} files"
        )


        with ProcessPoolExecutor(
            max_workers=workers
        ) as ex:

            results=list(
                ex.map(
                    analyze_file,
                    files
                )
            )


        data=[]

        for r in results:
            data.extend(r)


        data.sort(
            key=lambda x:x[0]
        )


        tuple_data[t]=data



    for mode in ["straight","2x2"]:


        plt.figure(
            figsize=(10,6)
        )


        for t,data in tuple_data.items():


            if mode=="straight":
                y=[
                    x[1]
                    for x in data
                ]

            else:
                y=[
                    x[2]
                    for x in data
                ]


            ma=moving_average(
                y,
                WINDOW
            )


            plt.plot(
                ma*100,
                label=f"{t}tuple"
            )


        plt.xlabel(
            f"Game ({WINDOW} moving average)"
        )


        plt.ylabel(
            "Ratio (%)"
        )


        plt.title(
            mode
        )


        plt.grid()

        plt.legend()

        plt.tight_layout()


        plt.savefig(
            f"{mode}_ratio_ma{WINDOW}.png",
            dpi=300
        )


        plt.close()


    print("Done")