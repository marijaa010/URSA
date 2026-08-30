/* Maximum of a symbolic array (5 elements): after the scan no element
   exceeds nmax, so nA[nk] <= nmax for every symbolic index nk < 5. UNSAT. */
nmax = nA[0];
for(ni=1; ni<5; ni++)
    nmax = ite(nA[ni] > nmax, nA[ni], nmax);
assert(nk < 5 && nA[nk] > nmax);
