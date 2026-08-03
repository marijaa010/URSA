/**
 * @file SMTSolverDriver.hpp
 * @brief Solves URSA-emitted SMT-LIB in-process using a linked SMT solver
 *        (Z3 or cvc5) and prints the resulting models in URSA's SAT-path
 *        format.
 *
 * For `assert_all` the driver enumerates all satisfying models by adding
 * blocking clauses between successive checks. For `minimize` / `maximize`
 * it reports the optimum (Z3 only; cvc5 does not support optimization).
 */

#ifndef __SMT_SOLVER_DRIVER_HPP
#define __SMT_SOLVER_DRIVER_HPP

#include <string>
#include "CLIOptions.hpp"

/**
 * @brief Runs an in-process SMT solving session.
 */
class SMTSolverDriver {
public:
    /**
     * @param smtBuffer   Complete SMT-LIB source (no check-sat/get-model).
     * @param solver      Which linked solver to use (Z3 or cvc5).
     * @param assertAll   True for `assert_all`, enabling model enumeration.
     * @param hasOptimize True if the program had minimize/maximize.
     */
    SMTSolverDriver(std::string smtBuffer,
                    eSMTSolver solver,
                    bool assertAll,
                    bool hasOptimize)
        : m_buffer(std::move(smtBuffer)), m_solver(solver),
          m_assertAll(assertAll), m_hasOptimize(hasOptimize) {}

    /**
     * @brief Runs the solver session end to end.
     * @return 0 on success (including `unsat`), non-zero on error.
     */
    int run();

private:
    std::string m_buffer;
    eSMTSolver m_solver;
    bool m_assertAll;
    bool m_hasOptimize;
};

#endif
