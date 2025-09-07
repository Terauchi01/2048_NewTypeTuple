NT4 = [[0,1,2,3],
       [3,4,5,6],
       [0,1,3,4]]

NT6 = [[0,1,2,3,4,5],
       [0,1,2,3,4,6]]

def genCode(index, tuples):
    for (ind, tup) in enumerate(tuples):
        print(f'beginfig({index * 10 + ind});')
        print('u := 1cm; pickup pencircle scaled .75;')
        print('for i = 0 upto 3: draw (0,i*u)--(3u,i*u); draw (i*u,0)--(i*u,3u); endfor;')
        for p in tup:
            for q in tup:
                if p == q: continue
                xp, yp = p % 3, p // 3
                xq, yq = q % 3, q // 3
                if (abs(xp - xq) == 1 and yp == yq) or (abs(yp - yq) == 1 and xp == xq):
                    print(f'draw (({xp}.5u, {yp}.5u)--({xq}.5u, {yq}.5u)) withpen pencircle scaled .1u;')
        for p in tup:
            x, y = p % 3, p // 3
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

genCode(4, NT4)
genCode(6, NT6)

print('end.')
