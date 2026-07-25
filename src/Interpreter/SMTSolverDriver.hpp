/**
 * @file SMTSolverDriver.hpp
 * @brief Runs an external SMT solver on URSA-emitted SMT-LIB and prints
 *        the resulting models in URSA's SAT-path format.
 *
 * For `assert_all` the driver enumerates all satisfying models by
 * adding blocking clauses between successive `(check-sat)` calls. For
 * `minimize` / `maximize` it reports the objective and its witness.
 */

#ifndef __SMT_SOLVER_DRIVER_HPP
#define __SMT_SOLVER_DRIVER_HPP

#include <string>
#include <vector>
#include <utility>
#include "SMTSolverBackend.hpp"

/**
 * @brief Encapsulates the `-smtsolve` runtime.
 */
class SMTSolverDriver {
public:
    /**
     * @param smtBuffer   Complete SMT-LIB source (no check-sat/get-model).
     * @param backend     Which solver to drive (Z3, cvc5, …).
     * @param assertAll   True for `assert_all`, enabling model enumeration.
     * @param hasOptimize True if the program had minimize/maximize.
     *                    Skips the enumeration loop and calls
     *                    `(get-objectives)` once.
     */
    SMTSolverDriver(std::string smtBuffer,
                    const ISMTSolverBackend& backend,
                    bool assertAll,
                    bool hasOptimize)
        : m_buffer(std::move(smtBuffer)), m_backend(backend),
          m_assertAll(assertAll), m_hasOptimize(hasOptimize) {}

    /**
     * @brief Runs the solver session end to end.
     * @return 0 on success (including `unsat`), non-zero on I/O or fork errors.
     */
    int run();

private:
    /** @brief A free variable's name and its emitted sort. */
    struct FreeVar {
        std::string name;
        std::string sort;
    };

    /** @brief A model entry: variable name and raw SMT-LIB value token. */
    struct Value {
        std::string name;
        std::string rawValue;
    };

    /**
     * @brief Scans the buffered SMT-LIB for `(declare-fun N () S)` lines
     *        and returns the resulting list. Array sorts are dropped
     *        (blocking clauses over arrays are out of scope).
     */
    std::vector<FreeVar> extractFreeVars() const;

    /**
     * @brief Forks a child process, execs the solver, sets up bidirectional pipes.
     *
     * @param[out] solverIn   Parent's writable end (child's stdin).
     * @param[out] solverOut  Parent's readable end (child's stdout).
     * @param[out] childPid   Child process id, needed for `waitpid`.
     * @return true on success.
     */
    bool spawnSolver(int& solverIn, int& solverOut, pid_t& childPid);

    /**
     * @brief Writes the entire buffer to `fd`, handling partial writes.
     * @return true if all bytes were written.
     */
    static bool writeAll(int fd, const std::string& s);

    /**
     * @brief Reads one non-empty line (stripping `\r`) from `fd`.
     *        Blocks until a full line is available or EOF.
     */
    static std::string readLine(int fd);

    /**
     * @brief Reads characters from `fd` until a `(` opens and its
     *        matching `)` closes at depth zero.
     *        Used to consume `(get-value ...)` / `(get-objectives)` responses,
     *        which may span multiple lines.
     */
    static std::string readBalanced(int fd);

    /**
     * @brief Parses a `((n1 v1) (n2 v2) ...)` response into a Value list.
     */
    static std::vector<Value> parseGetValueResponse(const std::string& s);

    /**
     * @brief Converts an SMT-LIB atomic value into a human-readable
     *        decimal string:
     *          `#x03`  → `3`
     *          `#b101` → `5`
     *          `(- 5)` → `-5`
     *          decimal or `true`/`false` → pass-through.
     */
    static std::string toDecimal(const std::string& v);

    /**
     * @brief Builds `(get-value (v1 v2 ...))\n` from a FreeVar list.
     */
    static std::string buildGetValueCommand(const std::vector<FreeVar>& vars);

    /**
     * @brief Builds `(assert (not (and (= v1 val1) (= v2 val2) ...)))\n`
     *        using RAW values so they match the solver's type expectations.
     */
    static std::string buildBlockingClause(const std::vector<Value>& values);

    /**
     * @brief Emits one solution to std::cout in URSA's SAT-path format:
     *          `--> Solution N`   (only for assert_all)
     *          `name=decimal;`    (one per free variable)
     *          blank line          (only for assert_all)
     */
    void printSolution(int solutionNumber, const std::vector<Value>& values) const;

    /**
     * @brief Handles the trivial case where the buffer contains no
     *        `(declare-fun`) at all, i.e. either `yes (trivially)` or
     *        `no (trivially)` short-circuited by SMT_Interpreter.
     *        Simply forwards the buffer to std::cout.
     */
    void printTrivial() const;

    std::string m_buffer;
    const ISMTSolverBackend& m_backend;
    bool m_assertAll;
    bool m_hasOptimize;
};

#endif
