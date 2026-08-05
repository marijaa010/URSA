/* Reversing an array twice yields the original array (reversal is an
   involution), for any 5-element array. UNSAT. */
for(ni=0; ni<5; ni++) nB[ni] = nA[4-ni];
for(ni=0; ni<5; ni++) nC[ni] = nB[4-ni];
assert(nC[0]!=nA[0] || nC[1]!=nA[1] || nC[2]!=nA[2] || nC[3]!=nA[3] || nC[4]!=nA[4]);
