for (ni = 0; ni < 3; ni++)
    for (nj = 0; nj < 3; nj++)
        nM[ni][nj] = ni * 3 + nj;

assert(nM[nr][nc] == 5 && nr < 3 && nc < 3);
