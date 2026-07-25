/**
 * @file SMTSolverBackend.hpp
 * @brief Per-solver invocation knowledge for `-smtsolve` mode.
 *
 * Each supported SMT solver has its own subclass of ISMTSolverBackend
 * describing how to launch it. Adding a new solver means adding one
 * class here and one enum value in CLIOptions.
 */

#ifndef __SMT_SOLVER_BACKEND_HPP
#define __SMT_SOLVER_BACKEND_HPP

#include <string>
#include <vector>
#include <cstdlib>

/**
 * @brief Abstract solver backend used by SMTSolverDriver.
 *
 * All methods are const and side-effect free. Implementations are
 * typically stateless singletons.
 */
class ISMTSolverBackend {
public:
    virtual ~ISMTSolverBackend() = default;

    /**
     * @brief Human-readable name of the solver (e.g., "z3", "cvc5").
     */
    virtual const char* name() const = 0;

    /**
     * @brief Name of the environment variable that overrides the binary path.
     *        Convention: `URSA_<SOLVER>` (e.g., `URSA_Z3`).
     */
    virtual const char* envVarName() const = 0;

    /**
     * @brief Default binary name looked up on `$PATH` when the env var is unset.
     */
    virtual const char* defaultBinary() const = 0;

    /**
     * @brief Extra arguments to pass after argv[0]. Excludes the binary path itself.
     *
     * Must enable: reading SMT-LIB v2 from stdin (or file `-`), incremental
     * mode (multiple `(check-sat)` calls interleaved with `(assert)`), and
     * model production (so `(get-value)` after `(check-sat) → sat` works).
     */
    virtual std::vector<std::string> extraArgs() const = 0;

    /**
     * @brief Whether this solver supports SMT-LIB `(minimize ...)` and
     *        `(maximize ...)` directives. Z3 does, cvc5 does not.
     */
    virtual bool supportsOptimize() const { return false; }

    /**
     * @brief Resolves the binary path from the environment variable, falling
     *        back to `defaultBinary()`.
     */
    std::string resolveBinary() const {
        const char* v = std::getenv(envVarName());
        return v ? std::string(v) : std::string(defaultBinary());
    }
};

/**
 * @brief Z3 backend. Invocation: `z3 -in`.
 *
 * `-in` tells Z3 to read the SMT-LIB session from stdin. Z3 emits
 * models on `(get-value)` after a `sat` verdict by default, so no
 * additional option is needed.
 */
class Z3Backend : public ISMTSolverBackend {
public:
    const char* name() const override { return "z3"; }
    const char* envVarName() const override { return "URSA_Z3"; }
    const char* defaultBinary() const override { return "z3"; }
    std::vector<std::string> extraArgs() const override {
        return { "-in" };
    }
    bool supportsOptimize() const override { return true; }
};

/**
 * @brief cvc5 backend. Invocation: `cvc5 --lang smt2 --produce-models --incremental -`.
 *
 * cvc5 requires explicit flags:
 *   - `--lang smt2`: read SMT-LIB v2.
 *   - `--produce-models`: enable `(get-value)` after `sat`.
 *   - `--incremental`: allow interleaved `(assert)` and `(check-sat)`.
 *   - `-`: read from stdin (as opposed to a file path argument).
 */
class CVC5Backend : public ISMTSolverBackend {
public:
    const char* name() const override { return "cvc5"; }
    const char* envVarName() const override { return "URSA_CVC5"; }
    const char* defaultBinary() const override { return "cvc5"; }
    std::vector<std::string> extraArgs() const override {
        return { "--lang", "smt2", "--produce-models", "--incremental", "-" };
    }
};

#endif
