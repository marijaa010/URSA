/* Idempotent write: writing nA[ni]'s current value leaves the array
   unchanged, so a symbolic read nA[nj] keeps its original value. UNSAT. */

nOld = nA[nj];
nA[ni] = nA[ni];
assert(nA[nj] != nOld);
