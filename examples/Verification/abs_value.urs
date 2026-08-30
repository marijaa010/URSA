/* Absolute value over the integers (QF_LIA): abs(nx) is non-negative and
   equals nx or -nx, for any nx. UNSAT. */
nAbs = ite(nx < 0, -nx, nx);
assert(nAbs < 0 || (nAbs != nx && nAbs != -nx));
