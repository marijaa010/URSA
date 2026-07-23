for (ni = 0; ni < 3; ni++)
    for (nj = 0; nj < 3; nj++)
        nM[ni][nj] = 0;

nM[nr][nc] = 99;

assert(nr < 3 && nc < 3 && nM[1][1] == 99);
