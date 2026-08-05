/* Job-shop scheduling, 3 jobs x 3 machines. Operations within a job run in
   order; each machine runs at most one operation at a time (disjunctive
   ordering). A schedule of makespan 9 exists, while makespan 8 is infeasible,
   so 9 is optimal for this instance. Intended for -smtlogic=QF_LIA. */
nK = 9;
bPrec = ns01 >= ns00+3 && ns02 >= ns01+2
     && ns11 >= ns10+2 && ns12 >= ns11+3
     && ns21 >= ns20+2 && ns22 >= ns21+3;
bNonNeg = ns00>=0 && ns01>=0 && ns02>=0 && ns10>=0 && ns11>=0 && ns12>=0 && ns20>=0 && ns21>=0 && ns22>=0;
bFinish = ns02+2 <= nK && ns12+1 <= nK && ns22+1 <= nK;
bM0 = ((ns00+3 <= ns11) || (ns11+3 <= ns00))
   && ((ns00+3 <= ns22) || (ns22+1 <= ns00))
   && ((ns11+3 <= ns22) || (ns22+1 <= ns11));
bM1 = ((ns01+2 <= ns10) || (ns10+2 <= ns01))
   && ((ns01+2 <= ns21) || (ns21+3 <= ns01))
   && ((ns10+2 <= ns21) || (ns21+3 <= ns10));
bM2 = ((ns02+2 <= ns12) || (ns12+1 <= ns02))
   && ((ns02+2 <= ns20) || (ns20+2 <= ns02))
   && ((ns12+1 <= ns20) || (ns20+2 <= ns12));
assert(bNonNeg && bPrec && bFinish && bM0 && bM1 && bM2);
