/* Counts the input arrays of five elements that selection sort maps to
   [0,1,2,3,4]. These inputs are exactly the permutations of {0,1,2,3,4},
   so the number of solutions is 5! = 120. Exercises symbolic-index
   read/write and all-solution enumeration over an array input. */

for(ni=0; ni<5; ni++) {
    nmin_idx = ni;
    for(nj=ni+1; nj<5; nj++)
        nmin_idx = ite(nA[nj] < nA[nmin_idx], nj, nmin_idx);
    nTmp = nA[ni];
    nA[ni] = nA[nmin_idx];
    nA[nmin_idx] = nTmp;
}

assert_all(nA[0]==0 && nA[1]==1 && nA[2]==2 && nA[3]==3 && nA[4]==4);
