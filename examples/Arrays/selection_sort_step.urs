/* Bounded verification of one step of selection sort (BMC, Armando et
   al. 2006), instance n = 5, i = 0: after the step, position 0 holds
   the minimum of the range. Uses symbolic-index read/write. UNSAT. */

nmin_idx = 0;
for(nj=1; nj<5; nj++)
    nmin_idx = ite(nA[nj] < nA[nmin_idx], nj, nmin_idx);

nTmp = nA[0];
nA[0] = nA[nmin_idx];
nA[nmin_idx] = nTmp;

assert(nA[0] > nA[1] || nA[0] > nA[2] || nA[0] > nA[3] || nA[0] > nA[4]);
