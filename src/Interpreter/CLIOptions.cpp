#include "CLIOptions.hpp"

#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

extern unsigned int iAbstractNumberLength;
extern bool bQuiet;
extern bool bDimacsOnly;
extern bool bMapping;
extern bool bCoherentLogicProofExport;
extern bool bSMTMode;
extern eSMTLogic bSMTLogic;
extern bool bSMTSolveMode;
extern eSMTSolver bSMTSolver;
extern const char* sSMTOutPath;
extern eSolvers URSASolver;

using namespace std;

void CLIOptions::applyToGlobals() const {
    iAbstractNumberLength = abstractNumberLength;
    bQuiet = quiet;
    bDimacsOnly = dimacsOnly;
    bMapping = mapping;
    bCoherentLogicProofExport = coherentLogicProofExport;
    bSMTMode = smtMode;
    bSMTLogic = smtLogic;
    bSMTSolveMode = smtSolveMode;
    bSMTSolver = smtSolver;
    sSMTOutPath = smtOutPath;
    URSASolver = satSolver;
}

const char* CLIOptions::validate() const {
    if (smtOutPath != nullptr && smtSolveMode) {
        return "-smtout and -smtsolve cannot be combined.";
    }
    return nullptr;
}

CLIOptions parseCLIArgs(int argc, char** argv) {
    CLIOptions opts;
    int len;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') continue;

        if (!strcmp(argv[i], "-smt")) { opts.smtMode = true; continue; }
        if (!strcmp(argv[i], "-smtlogic=QF_LIA")) {
#ifndef GMP_SUPPORT
            cerr << "ERROR: QF_LIA requires arbitrary-precision integers, "
                 << "which need GMP. This build was made with GMP_SUPPORT=0; "
                 << "rebuild with GMP enabled to use QF_LIA." << endl;
            exit(1);
#endif
            opts.smtMode = true; opts.smtLogic = eLogicQF_LIA; continue;
        }
        if (!strcmp(argv[i], "-smtlogic=QF_BV")) {
            opts.smtMode = true; opts.smtLogic = eLogicQF_BV; continue;
        }
        if (!strcmp(argv[i], "-smtsolve=z3")) {
            opts.smtMode = true; opts.smtSolveMode = true; opts.smtSolver = eSolverZ3;
            continue;
        }
        if (!strcmp(argv[i], "-smtsolve=cvc5")) {
            opts.smtMode = true; opts.smtSolveMode = true; opts.smtSolver = eSolverCVC5;
            continue;
        }
        if (!strncmp(argv[i], "-smtout=", 8)) {
            opts.smtMode = true; opts.smtOutPath = argv[i] + 8; continue;
        }

        switch (argv[i][1]) {
        case 'l':
            if (sscanf(argv[i] + 2, "%i", &len) == 1) {
                opts.abstractNumberLength = len;
            } else {
                cout << "A number after the -l option expected." << endl << endl;
                opts.helpRequested = true;
                return opts;
            }
            break;
        case 's': {
            for (char* p = argv[i]; *p; p++) *p = tolower(*p);
            if (!strcmp(argv[i] + 2, "argosat")) opts.satSolver = eArgoSAT;
            else if (!strcmp(argv[i] + 2, "minisat")) opts.satSolver = eMiniSAT;
            else if (!strcmp(argv[i] + 2, "clasp")) opts.satSolver = eClasp;
            else {
                cerr << "ERROR: unknown option '" << argv[i] << "'." << endl << endl;
                opts.helpRequested = true;
                return opts;
            }
            break;
        }
        case 'd': opts.dimacsOnly = true; break;
        case 'q': opts.quiet = true; break;
        case 'c': opts.coherentLogicProofExport = true; break;
        case 'm': opts.mapping = true; break;
        case 'h': opts.helpRequested = true; break;
        default:
            cerr << "ERROR: unknown option '" << argv[i] << "'." << endl << endl;
            opts.helpRequested = true;
            return opts;
        }
    }
    return opts;
}

void printCLIHelp() {
    cout << "Usage: ./ursa [OPTIONS] ..." << endl << endl;
    cout << "Solves specified problems by reducing them to SAT." << endl << endl;
    cout << "Options:" << endl;
    cout << "-l - sets the number of bits that represent numbers (e.g., -l10; default value is 8)" << endl;
    cout << "-d - DIMACS output only" << endl;
    cout << "-q - quite mode (models are not printed out)" << endl;
    cout << "-m - prints mapping between URSA variables and SAT variables" << endl;
    cout << "-s - selects an underlying solvers (e.g., -sargosat, -sclasp, -sminisat; defaulf is clasp)" << endl;
    cout << "-smt - emit SMT-LIB output instead of running a SAT solver (default logic: QF_BV)" << endl;
    cout << "-smtlogic=QF_BV|QF_LIA - choose the SMT-LIB logic (implies -smt; default QF_BV)" << endl;
    cout << "-smtsolve=z3|cvc5 - solve in-process via the linked solver library (implies -smt)." << endl;
    cout << "                    z3 supports minimize/maximize; cvc5 does not." << endl;
    cout << "-smtout=<path> - write clean SMT-LIB to the given file (implies -smt)." << endl;
    cout << "                 Banner and stats stay on stdout/stderr." << endl << endl;
    cout << "Example:" << endl;
    cout << "./ursa -l10 < examples/CSP/queens.urs" << endl;
}
