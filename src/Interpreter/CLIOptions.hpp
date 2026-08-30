/**
 * @file CLIOptions.hpp
 * @brief Parsed command-line options for the URSA binary.
 *
 * Centralizes the full CLI surface. Every flag URSA accepts is
 * declared here, together with the rules for how flags interact
 * (which imply which, which are mutually exclusive).
 */

#ifndef __CLI_OPTIONS_HPP
#define __CLI_OPTIONS_HPP

typedef enum { eArgoSAT, eClasp, eMiniSAT } eSolvers;
typedef enum { eLogicQF_BV, eLogicQF_LIA } eSMTLogic;
typedef enum { eSolverZ3, eSolverCVC5 } eSMTSolver;

/**
 * @brief Parsed CLI state. Populated by parseCLIArgs, applied to globals
 *        with applyToGlobals(). Defaults match URSA's historical behavior.
 */
struct CLIOptions {
    unsigned int abstractNumberLength = 8;
    eSolvers satSolver = eClasp;
    bool quiet = false;
    bool dimacsOnly = false;
    bool mapping = false;
    bool coherentLogicProofExport = false;

    bool smtMode = false;
    eSMTLogic smtLogic = eLogicQF_BV;
    bool smtSolveMode = false;
    eSMTSolver smtSolver = eSolverZ3;
    const char* smtOutPath = nullptr;

    /** @brief True if `-h` was passed. */
    bool helpRequested = false;

    /**
     * @brief Copies fields into extern globals used elsewhere in the codebase.
     */
    void applyToGlobals() const;

    /** @brief Validates cross-flag constraints. Returns error message or nullptr. */
    const char* validate() const;
};

/**
 * @brief Parses argv into a CLIOptions struct.
 *
 * On invalid input (e.g., missing number for `-l`), prints a message and
 * returns a struct with `helpRequested = true` (main should exit).
 *
 * @param argc Number of arguments.
 * @param argv Argument vector as passed to main().
 * @return Populated options.
 */
CLIOptions parseCLIArgs(int argc, char** argv);

/**
 * @brief Prints the CLI help text to stdout.
 */
void printCLIHelp();

#endif
