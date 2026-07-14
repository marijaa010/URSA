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


class SMTInterpreter {
public:
   SMTInterpreter()
       : m_hasOptimization(false), m_optMin(0), m_optMax(0), m_optMaximize(false)
   { m_Timer.StartMeasuringTime(); }
   ~SMTInterpreter() { m_ST.Clear(); ClearProgram(); }

   int ExecuteCommand(nodeType *p);
   void RecordCommand(nodeType *p);
   void RecordProcedure(nodeType *p);

private:
   std::vector<nodeType *> URSAprogram;

   bool ExecuteProcedure(nodeType *p);

   void ClearCommand(nodeType *p);
   void ClearProgram();
   bool Solve(nodeType *p, bool bAllSolutions);
   bool SolveConstraint(nodeType *p, bool bAllSolutions);

   void ExecuteCommandTree(nodeType *p);
   void PrintProcedure();
   void PrintCommand(nodeType *p);

   SMTNumber ReadNumber(nodeType *p);
   SMTBoolean ReadBoolean(nodeType *p);
   bool IsNumberId(nodeType *p);
   bool IsBooleanId(nodeType *p);

   SMTSymbolTable m_ST;
   std::vector<SMTBoolean> m_assertions;

   bool m_hasOptimization = false;
   std::string m_optVarName;
   uint64_t m_optMin = 0;
   uint64_t m_optMax = 0;
   bool m_optMaximize = false;

   CTimer m_Timer;
};

#endif
