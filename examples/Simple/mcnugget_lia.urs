/* Frobenius coin problem (Chicken McNugget), pack sizes 6, 9, 20:
   43 is the largest total that cannot be formed, so over non-negative
   integers (QF_LIA) this is UNSAT. */
assert(na>=0 && nb>=0 && nc>=0 && 6*na + 9*nb + 20*nc == 43);
