/* Mini job-shop scheduling (Bofill et al. 2012). Two jobs of two
   ordered operations on two machines; nStart[i][j] is the start time
   of operation j of job i. Asks for a schedule with makespan at most
   12. Intended for -smtlogic=QF_LIA. */

bNonNeg = nStart[0][0] >= 0 && nStart[0][1] >= 0
       && nStart[1][0] >= 0 && nStart[1][1] >= 0;

bJobOrder0 = nStart[0][1] >= nStart[0][0] + 3;
bJobOrder1 = nStart[1][1] >= nStart[1][0] + 4;

bMachine0 = (nStart[0][0] + 3 <= nStart[1][1]) || (nStart[1][1] + 3 <= nStart[0][0]);
bMachine1 = (nStart[0][1] + 2 <= nStart[1][0]) || (nStart[1][0] + 4 <= nStart[0][1]);

bMakespan = nStart[0][1] + 2 <= 12 && nStart[1][1] + 3 <= 12;

assert(bNonNeg && bJobOrder0 && bJobOrder1 && bMachine0 && bMachine1 && bMakespan);
