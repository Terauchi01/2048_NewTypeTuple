NT6G = [[0,1,2,4,5,6],
        [1,2,5,6,9,13],
        [0,1,2,3,4,5],
        [0,1,5,6,7,10],
        [0,1,2,5,9,10],
        [0,1,5,9,13,14],
        [0,1,5,8,9,13],
        [0,1,2,4,6,10]]

NT6 = [[0,1,2,4,5,6],
       [4,5,6,8,9,10],
       [0,1,2,3,6,7],
       [4,5,6,7,10,11],
       [8,9,10,11,14,15],
       [0,1,2,3,7,11],
       [4,5,6,7,11,15],
       [3,6,7,9,10,11],
       [7,10,11,13,14,15]]

NT7 = [[0,1,2,3,5,6,7],
       [4,5,6,7,9,10,11],
       [8,9,10,11,13,14,15],
       [0,1,2,3,6,7,11],
       [4,5,6,7,10,11,15],
       [0,1,2,3,7,11,15],
       [7,9,10,11,13,14,15],
       [3,5,6,7,9,10,11]]

NT8 = [[0,1,2,3,4,5,6,7],
       [4,5,6,7,8,9,10,11],
       [0,1,2,3,6,7,11,15],
       [2,3,5,6,7,9,10,11],
       [6,7,9,10,11,13,14,15]]

NT9 = [[0,1,2,4,5,6,8,9,10],
       [4,5,6,8,9,10,12,13,14],
       [0,1,2,3,4,5,6,7,11],
       [4,5,6,7,8,9,10,11,15],
       [0,1,2,3,5,6,7,11,15],
       [0,1,2,3,5,6,7,10,11],
       [4,5,6,7,9,10,11,14,15]]

def genCode(index, tuples):
    for (ind, tup) in enumerate(tuples):
        print(f'beginfig({index * 10 + ind});')
        print('u := 1cm; pickup pencircle scaled .75;')
        print('for i = 0 upto 4: draw (0,i*u)--(4u,i*u); draw (i*u,0)--(i*u,4u); endfor;')
        for p in tup:
            for q in tup:
                if p == q: continue
                xp, yp = p % 4, p // 4
                xq, yq = q % 4, q // 4
                if (abs(xp - xq) == 1 and yp == yq) or (abs(yp - yq) == 1 and xp == xq):
                    print(f'draw (({xp}.5u, {yp}.5u)--({xq}.5u, {yq}.5u)) withpen pencircle scaled .1u;')
        for p in tup:
            x, y = p % 4, p // 4
            print(f'fill fullcircle scaled .45u shifted ({x}.5u, {y}.5u);')
            print(f'unfill fullcircle scaled .3u shifted ({x}.5u, {y}.5u);')
        print('currentpicture := currentpicture yscaled -1;')
        print('endfig;')

print(r'''verbatimtex
%&latex
\documentclass[10pt]{article}
\usepackage{latexsym}
\usepackage{times}
\begin{document}
\small
\sf
etex
''')

genCode(0, NT6G)
genCode(6, NT6)
genCode(7, NT7)
genCode(8, NT8)
genCode(9, NT9)

print('end.')
