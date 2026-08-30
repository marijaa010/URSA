/* Sunzi's remainder problem (~5th century), origin of the Chinese
   Remainder Theorem: the smallest x with x mod 3 = 2, x mod 5 = 3 and
   x mod 7 = 2 is 23. Uses the remainder operator, available only via
   the SMT path (QF_LIA); the SAT reduction does not support it. */
assert_all(nx > 0 && nx < 105 && nx % 3 == 2 && nx % 5 == 3 && nx % 7 == 2);
