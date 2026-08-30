/* Distributivity over bit-vectors mod 2^L:
   na*(nb+nc) == na*nb + na*nc. Identity holds, so UNSAT. */

assert(na * (nb + nc) != na * nb + na * nc);
