/* Store shadowing: nA[ni] = 1; nA[nj] = 2; then nA[ni] == 1 fails
   when ni == nj (the second write overwrites the first). */

nA[ni] = 1;
nA[nj] = 2;
assert(nA[ni] != 1);
