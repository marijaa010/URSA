/* Arbitrary-precision reasoning in QF_LIA. The value 10^20 lies beyond the
   64-bit range, so a fixed-width encoding would wrap it around. The unique
   even integer strictly between 10^20 and 10^20 + 3 is found exactly. */
assert(nx > 100000000000000000000 && nx < 100000000000000000003 && nx % 2 == 0);
