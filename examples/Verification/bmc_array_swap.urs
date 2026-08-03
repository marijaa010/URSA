/* BMC of an in-place swap through symbolic indices (Armando et al.
   2006): after the swap nA[ni] equals the old nA[nj], for any
   aliasing of ni and nj. UNSAT. */

nOldNj = nA[nj];

nTmp    = nA[ni];
nA[ni]  = nA[nj];
nA[nj]  = nTmp;

assert(nA[ni] != nOldNj);
