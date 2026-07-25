/**
 * @file SMT_Interpreter.hpp
 * @brief Interpreter that walks the URSA AST and emits SMT-LIB output.
 *
 * SMTInterpreter is the SMT-side counterpart to the SAT-side Interpreter
 * It walks the same AST but, instead of building a propositional formula
 * through FormulaFactory, it constructs SMTExpr nodes through SMTFactory
 * and emits SMT-LIB text to std::cout when a constraint assertion is finalized.
 *
 * Program flow:
 *   1. RecordCommand / RecordProcedure: the parser drops top-level
 *      commands and procedures here, and SMTInterpreter stores them for
 *      later execution.
 *   2. ExecuteCommand: walks a single command, updating m_ST with any
 *      new variable bindings, folding ground arithmetic, and delaying
 *      symbolic operations by building SMTExpr subtrees.
 *   3. On ASSERT / ASSERTA (single-solution / all-solutions),
 *      Solve calls SolveConstraint, which accumulates a top-level
 *      constraint and emits the finished SMT-LIB (declarations and
 *      assertions).
 *
 * The output logic (QF_BV vs QF_LIA) is selected by the global
 * bSMTLogic. The presence of an ASSERTA is recorded in bSMTAssertAll
 * so that the -smtsolve wrapper can drive model enumeration.
 */

#ifndef __SMT_INTERPRETER_H
#define __SMT_INTERPRETER_H

#include "ursa.h"
#include "SMTNumber.hpp"
#include "SMTBoolean.hpp"
#include "SMTSymbolTable.hpp"
#include "SymbolTable.hpp"
#include "Timer.h"
#include <vector>
#include <string>
#include <map>


/**
 * @brief SMT-LIB emission interpreter (see file header for architecture).
 */
class SMTInterpreter {
public:
   SMTInterpreter()
       : m_hasOptimization(false), m_optMin(0), m_optMax(0), m_optMaximize(false)
   { m_Timer.StartMeasuringTime(); }
   ~SMTInterpreter() { m_ST.Clear(); ClearProgram(); }

   /**
    * @brief Executes one AST node (statement, procedure call, assertion).
    *
    * Called by the parser for every top-level command. Dispatches on node
    * type via a large switch. Assignment updates symbol table, and
    * ASSERT / ASSERTA finalize output.
    *
    * @param p AST node to execute. Ownership stays with the parser.
    * @return  0 on expected completion.
    */
   int ExecuteCommand(nodeType *p);

   /**
    * @brief Registers a top-level command in the interpreter's program
    *        list so it can be replayed later (e.g., inside procedure calls).
    */
   void RecordCommand(nodeType *p);

   /**
    * @brief Registers a `procedure` declaration in URSAprocedures so
    *        that later CALL nodes can be resolved.
    */
   void RecordProcedure(nodeType *p);

private:
   std::vector<nodeType *> URSAprogram;

   /**
    * @brief Executes a CALL by looking up the procedure in URSAprocedures
    *        and running its body with the arguments substituted.
    */
   bool ExecuteProcedure(nodeType *p);

   /** @brief Frees the AST subtree rooted at `p`. */
   void ClearCommand(nodeType *p);

   /** @brief Empties URSAprogram, freeing all registered commands. */
   void ClearProgram();

   /**
    * @brief Entry point for ASSERT / ASSERTA. Delegates to SolveConstraint.
    * @param bAllSolutions Passed straight through.
    */
   bool Solve(nodeType *p, bool bAllSolutions);

public:
   /** @brief True if any ASSERTA was encountered. The `-smtsolve` wrapper
    *         uses this to decide whether to enumerate models. */
   bool wasAssertAll() const { return m_wasAssertAll; }
   /** @brief True if any minimize/maximize was encountered. The wrapper
    *         then invokes `(get-objectives)` once and skips enumeration. */
   bool hasOptimize() const { return m_hasOptimization; }

private:

   /**
    * @brief Emits the SMT-LIB output for the accumulated constraint.
    *
    * Reads the constraint AST, folds ground subexpressions, appends the
    * result to m_assertions, then emits:
    *   - `(set-logic ...)` (or none, if optimize path already set)
    *   - `(declare-fun ...)` for each free variable in the symbol table
    *   - `(assert ...)` for each accumulated top-level constraint
    *   - `(check-sat)` and `(get-model)`, only if !bSMTSolveMode.
    *
    * Also sets `m_wasAssertAll` (if bAllSolutions) and `m_hasOptimization`
    * (if a minimize/maximize was seen), so the wrapper can decide whether
    * to run a model-enumeration loop.
    *
    * @param p              AST node for the constraint expression.
    * @param bAllSolutions  Whether this came from `assert_all` vs `assert`.
    * @return true on non-trivial or trivially-sat constraint,
    *         false only for trivially-unsat.
    */
   bool SolveConstraint(nodeType *p, bool bAllSolutions);

   /** @brief Recursive AST-executor used by procedure bodies. */
   void ExecuteCommandTree(nodeType *p);

   /**
    * @brief Evaluates a numeric AST subtree, producing an SMTNumber.
    *        Ground arithmetic is folded, and symbolic parts remain as SMTExpr.
    */
   SMTNumber ReadNumber(nodeType *p);

   /**
    * @brief Evaluates a Boolean AST subtree, producing an SMTBoolean.
    */
   SMTBoolean ReadBoolean(nodeType *p);

   /** @brief True if `p` names a numeric variable in the symbol table. */
   bool IsNumberId(nodeType *p);
   /** @brief True if `p` names a Boolean variable in the symbol table. */
   bool IsBooleanId(nodeType *p);

   SMTSymbolTable m_ST;                    ///< Variables and arrays visible to this run.
   std::vector<SMTBoolean> m_assertions;   ///< Accumulated top-level constraints.

   bool m_wasAssertAll = false;            ///< True after at least one ASSERTA.
   bool m_hasOptimization = false;         ///< True after minimize/maximize seen.
   std::string m_optVarName;               ///< Variable being minimized/maximized.
   uint64_t m_optMin = 0;                  ///< User-supplied lower bound.
   uint64_t m_optMax = 0;                  ///< User-supplied upper bound.
   bool m_optMaximize = false;             ///< True for maximize, false for minimize.

   CTimer m_Timer;                         ///< Emission-time measurement.
};

#endif
