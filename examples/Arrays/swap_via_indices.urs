/* Swap through two symbolic indices ni, nj is its own inverse: after
   two swaps nA[ni] equals its original value, for any ni, nj. UNSAT. */

nA0 = nA[ni];

nT     = nA[ni];
nA[ni] = nA[nj];
nA[nj] = nT;

nT2     = nA[ni];
nA[ni]  = nA[nj];
nA[nj]  = nT2;

assert(nA[ni] != nA0);
