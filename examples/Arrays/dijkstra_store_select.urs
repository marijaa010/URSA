/* Dijkstra's store-at-nested-index question: after nA[nA[1]] = 2,
   nA[nA[1]] == 2 need not hold (it fails when the old nA[1] == 1). */

nA[nA[1]] = 2;
assert(nA[nA[1]] != 2);
