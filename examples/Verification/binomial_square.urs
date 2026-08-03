/* Binomial square over bit-vectors mod 2^L:
   (nx+ny)*(nx+ny) == nx*nx + 2*nx*ny + ny*ny. Identity holds, so UNSAT. */

assert((nx + ny)*(nx + ny) != nx*nx + 2*nx*ny + ny*ny);
