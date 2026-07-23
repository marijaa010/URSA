/************************************************************************************
URSA -- Copyright (c) 2010-2020, Predrag Janicic

This file is part of URSA
 
URSA is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
 
URSA is WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
**************************************************************************************/

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include "URSA_SATinterpreter.hpp"
#include "SMT_Interpreter.hpp"
#include "ursa.tab.hpp"
#include "FormulaFactory.h"
#include "SATsolver.h"
#include "ArgoSATSolver.h"
#include "ClaspSATSolver.h"
#include "MiniSATSolver.h"


using namespace std;

typedef enum { eArgoSAT, eClasp, eMiniSAT } eSolvers;
eSolvers URSASolver;
unsigned int iAbstractNumberLength;
bool bQuiet;
bool bDimacsOnly;
bool bMapping;
bool bCoherentLogicProofExport;
bool bSMTMode;
typedef enum { eLogicQF_BV, eLogicQF_LIA } eSMTLogic;
eSMTLogic bSMTLogic;
bool bSMTSolveMode;
typedef enum { eSolverZ3, eSolverCVC5 } eSMTSolver;
eSMTSolver bSMTSolver;
const char* sSMTOutPath;
Interpreter in;
SMTInterpreter smtIn;

unsigned int iVarCounter;

extern int yyparse ();
extern bool bSMTAssertAll;
extern bool bSMTHasOptimize;

void ClearCommand(nodeType *p) {
    int i;

    if (!p) return;
    if(p->type==typeId) 
      free(p->id.i);

    if (p->type == typeOpr) 
      for (i = 0; i < p->opr.nops; i++) 
        ClearCommand(p->opr.op[i]);

    free(p);
}


// ----------------------------------------------------------------------------


int main(int argc, char** argv) {
    int i, len;
    cout << "************************************************" << endl;
    cout << "****  URSA Interpreter v4.00 (c) 2010-2020  ****" << endl;
    cout << "*** Predrag Janicic,  University of Belgrade ***" << endl;
    cout << "************************************************" << endl << endl;
    
    iAbstractNumberLength=8;
    bQuiet=false;
    bDimacsOnly=false;
    bCoherentLogicProofExport=false;
    bMapping=false;
    bSMTMode=false;
    bSMTLogic=eLogicQF_BV;
    bSMTSolveMode=false;
    bSMTSolver=eSolverZ3;
    sSMTOutPath=nullptr;
    URSASolver = eClasp;

    for(i=1;i<argc;i++) {
      if(argv[i][0]=='-') {
         if(!strcmp(argv[i],"-smt")) { bSMTMode = true; continue; }
         if(!strcmp(argv[i],"-smtlogic=QF_LIA")) {
             bSMTMode = true; bSMTLogic = eLogicQF_LIA; continue;
         }
         if(!strcmp(argv[i],"-smtlogic=QF_BV")) {
             bSMTMode = true; bSMTLogic = eLogicQF_BV; continue;
         }
         if(!strcmp(argv[i],"-smtsolve=z3")) {
             bSMTMode = true; bSMTSolveMode = true; bSMTSolver = eSolverZ3; continue;
         }
         if(!strcmp(argv[i],"-smtsolve=cvc5")) {
             bSMTMode = true; bSMTSolveMode = true; bSMTSolver = eSolverCVC5; continue;
         }
         if(!strncmp(argv[i],"-smtout=",8)) {
             bSMTMode = true; sSMTOutPath = argv[i] + 8; continue;
         }
         switch(argv[i][1]) {
           case 'l':  if (sscanf(argv[i]+2,"%i",&len) == 1)  
                         iAbstractNumberLength = len;
                      else {
                         cout << "A number after the -l option expected." << endl << endl;
                         return false;
                      }
                      break;
           case 's':  {
                      char *p;
                      for (p=argv[i]; *p; p++ ) 
                        *p = tolower(*p);
                      if(!strcmp(argv[i]+2,"argosat"))
                        URSASolver = eArgoSAT;
                      if(!strcmp(argv[i]+2,"minisat"))
                        URSASolver = eMiniSAT;
                      break;
                      }
           case 'd':  bDimacsOnly=true; break;
           case 'q':  bQuiet=true; break;
           case 'c':  bCoherentLogicProofExport=true; break;
           case 'm':  bMapping=true; break;
           case 'h':  
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
                      cout << "-smtsolve=z3|cvc5 - emit SMT-LIB and pipe it to the chosen solver (z3 or cvc5)" << endl;
                      cout << "                    (implies -smt; solver binary read from URSA_Z3 / URSA_CVC5 env vars," << endl;
                      cout << "                     defaults 'z3' / 'cvc5' on PATH)" << endl;
                      cout << "-smtout=<path> - write clean SMT-LIB to the given file (implies -smt)." << endl;
                      cout << "                 Banner and stats stay on stdout/stderr." << endl << endl;
                      cout << "Example:" << endl;
                      cout << "./ursa -l10 < examples/CSP/queens.urs" << endl;
           default :  break;
         }
      }
    }

    iVarCounter=0;
    // yydebug=1;

    if (sSMTOutPath != nullptr && bSMTSolveMode) {
      cerr << "ERROR: -smtout and -smtsolve cannot be combined." << endl;
      return 1;
    }

    if (sSMTOutPath != nullptr) {
      static ofstream smtOutFile(sSMTOutPath);
      if (!smtOutFile.is_open()) {
        cerr << "ERROR: could not open " << sSMTOutPath << " for writing." << endl;
        return 1;
      }
      cout.rdbuf(smtOutFile.rdbuf());
    }

    if (bSMTSolveMode) {
      static ostringstream g_smtBuffer;
      static streambuf* g_oldCoutBuf = cout.rdbuf(g_smtBuffer.rdbuf());
      atexit([]() {
        if (g_oldCoutBuf != nullptr) {
          cout.rdbuf(g_oldCoutBuf);
          g_oldCoutBuf = nullptr;
        }
        string buffer = g_smtBuffer.str();

        if (buffer.find("(declare-fun") == string::npos) {
          cout << buffer;
          return;
        }

        vector<pair<string,string>> freeVars;
        {
          istringstream iss(buffer);
          string line;
          while (getline(iss, line)) {
            if (line.compare(0, 13, "(declare-fun ") != 0) continue;
            size_t nameStart = 13;
            size_t nameEnd = line.find(' ', nameStart);
            if (nameEnd == string::npos) continue;
            string name = line.substr(nameStart, nameEnd - nameStart);
            size_t sortStart = line.find("()", nameEnd);
            if (sortStart == string::npos) continue;
            sortStart += 2;
            while (sortStart < line.size() && line[sortStart] == ' ') sortStart++;
            size_t sortEnd = line.size();
            while (sortEnd > sortStart && line[sortEnd - 1] != ')') sortEnd--;
            if (sortEnd == sortStart) continue;
            string sort = line.substr(sortStart, sortEnd - sortStart - 1);
            if (sort.find("Array") != string::npos) continue;
            freeVars.push_back(make_pair(name, sort));
          }
        }

        const char* envVar = (bSMTSolver == eSolverZ3) ? "URSA_Z3" : "URSA_CVC5";
        const char* envValue = getenv(envVar);
        string solverBinary = envValue ? envValue :
                              (bSMTSolver == eSolverZ3 ? "z3" : "cvc5");

        int inPipe[2], outPipe[2];
        if (pipe(inPipe) < 0 || pipe(outPipe) < 0) {
          cerr << "ERROR: pipe() failed." << endl;
          return;
        }
        pid_t pid = fork();
        if (pid < 0) {
          cerr << "ERROR: fork() failed." << endl;
          return;
        }
        if (pid == 0) {
          dup2(inPipe[0], STDIN_FILENO);
          dup2(outPipe[1], STDOUT_FILENO);
          close(inPipe[0]); close(inPipe[1]);
          close(outPipe[0]); close(outPipe[1]);
          if (bSMTSolver == eSolverZ3) {
            execlp(solverBinary.c_str(), solverBinary.c_str(), "-in", (char*)nullptr);
          } else {
            execlp(solverBinary.c_str(), solverBinary.c_str(),
                   "--lang", "smt2", "--produce-models", "--incremental",
                   "-", (char*)nullptr);
          }
          _exit(127);
        }
        close(inPipe[0]);
        close(outPipe[1]);
        int solverIn = inPipe[1];
        int solverOut = outPipe[0];

        auto writeAll = [&](const string& s) {
          const char* p = s.data();
          size_t left = s.size();
          while (left > 0) {
            ssize_t w = write(solverIn, p, left);
            if (w <= 0) return false;
            p += w; left -= (size_t)w;
          }
          return true;
        };
        auto readLine = [&]() -> string {
          string line;
          char c;
          while (read(solverOut, &c, 1) == 1) {
            if (c == '\n') { if (line.empty()) continue; return line; }
            if (c != '\r') line += c;
          }
          return line;
        };
        auto readBalanced = [&]() -> string {
          string s;
          int depth = 0;
          bool started = false;
          char c;
          while (read(solverOut, &c, 1) == 1) {
            if (!started) {
              if (c == '(') { started = true; depth = 1; s += c; }
              continue;
            }
            s += c;
            if (c == '(') depth++;
            else if (c == ')') { depth--; if (depth == 0) return s; }
          }
          return s;
        };
        auto skipWs = [](const string& s, size_t& k) {
          while (k < s.size() && (s[k]==' '||s[k]=='\n'||s[k]=='\t'||s[k]=='\r')) k++;
        };
        auto parseGetValue = [&](const string& s) -> vector<pair<string,string>> {
          vector<pair<string,string>> out;
          size_t i = 0;
          skipWs(s, i);
          if (i >= s.size() || s[i] != '(') return out;
          i++;
          while (true) {
            skipWs(s, i);
            if (i >= s.size() || s[i] == ')') break;
            if (s[i] != '(') { i++; continue; }
            i++;
            skipWs(s, i);
            size_t nameStart = i;
            while (i < s.size() && s[i]!=' ' && s[i]!='\t' && s[i]!='\n' && s[i]!=')') i++;
            string name = s.substr(nameStart, i - nameStart);
            skipWs(s, i);
            string val;
            if (i < s.size() && s[i] == '(') {
              int d = 1; val += s[i++];
              while (i < s.size() && d > 0) {
                if (s[i]=='(') d++;
                else if (s[i]==')') d--;
                val += s[i++];
              }
            } else {
              while (i<s.size() && s[i]!=' ' && s[i]!=')' && s[i]!='\n') val += s[i++];
            }
            out.push_back(make_pair(name, val));
            skipWs(s, i);
            while (i < s.size() && s[i] != ')') i++;
            if (i < s.size()) i++;
          }
          return out;
        };
        auto toDecimal = [](const string& v) -> string {
          if (v.size() >= 2 && v[0]=='#' && (v[1]=='x'||v[1]=='X')) {
            unsigned long long n = 0;
            for (size_t k = 2; k < v.size(); k++) {
              char c = v[k]; int d;
              if (c>='0'&&c<='9') d=c-'0';
              else if (c>='a'&&c<='f') d=c-'a'+10;
              else if (c>='A'&&c<='F') d=c-'A'+10;
              else break;
              n = n*16 + d;
            }
            return to_string(n);
          }
          if (v.size() >= 2 && v[0]=='#' && (v[1]=='b'||v[1]=='B')) {
            unsigned long long n = 0;
            for (size_t k = 2; k < v.size(); k++) {
              if (v[k]!='0' && v[k]!='1') break;
              n = n*2 + (v[k]-'0');
            }
            return to_string(n);
          }
          if (v.size() > 3 && v[0]=='(' && v[1]=='-') {
            size_t s = 2; while (s < v.size() && v[s]==' ') s++;
            size_t e = v.find(')', s);
            if (e != string::npos) return "-" + v.substr(s, e-s);
          }
          return v;
        };

        if (!writeAll(buffer)) {
          cerr << "ERROR: could not write SMT-LIB to solver." << endl;
          close(solverIn); close(solverOut);
          waitpid(pid, nullptr, 0);
          return;
        }

        int solutionCount = 0;
        while (true) {
          if (!writeAll("(check-sat)\n")) break;
          string firstLine = readLine();
          if (firstLine == "unsat") break;
          if (firstLine == "unknown") {
            cerr << "Solver returned 'unknown' - could not determine satisfiability." << endl;
            break;
          }
          if (firstLine != "sat") {
            if (!firstLine.empty()) cerr << "Unexpected solver output: " << firstLine << endl;
            break;
          }

          if (bSMTHasOptimize) {
            writeAll("(get-objectives)\n");
            string objs = readBalanced();
            cout << "Objectives: " << objs << endl;
          }

          vector<pair<string,string>> values;
          if (!freeVars.empty()) {
            string cmd = "(get-value (";
            for (size_t k = 0; k < freeVars.size(); k++) {
              if (k) cmd += " ";
              cmd += freeVars[k].first;
            }
            cmd += "))\n";
            if (!writeAll(cmd)) break;
            string response = readBalanced();
            values = parseGetValue(response);
          }

          solutionCount++;
          if (bSMTAssertAll) cout << "--> Solution " << solutionCount << endl;
          for (size_t k = 0; k < values.size(); k++) {
            cout << values[k].first << "=" << toDecimal(values[k].second) << ";" << endl;
          }
          if (bSMTAssertAll) cout << endl;

          if (!bSMTAssertAll || bSMTHasOptimize) break;

          string block = "(assert (not (and";
          for (size_t k = 0; k < values.size(); k++) {
            block += " (= " + values[k].first + " " + values[k].second + ")";
          }
          block += ")))\n";
          if (!writeAll(block)) break;
        }

        writeAll("(exit)\n");
        close(solverIn);
        close(solverOut);
        waitpid(pid, nullptr, 0);

        if (solutionCount == 0) {
          cout << "No solutions found." << endl;
        } else if (bSMTAssertAll) {
          cerr << "[Number of solutions: " << solutionCount << "]" << endl;
        }
      });
    }
    yyparse();

    map<const string, nodeType *, lstr >::iterator it;
    for(it=URSAprocedures.begin();it!=URSAprocedures.end();it++)
      ClearCommand(it->second);
    URSAprocedures.clear();

    return 0;
}


// ----------------------------------------------------------------------------


int ex(nodeType *p) {
   if (bSMTMode) {
      smtIn.RecordCommand(p);
      return smtIn.ExecuteCommand(p);
   }
   in.RecordCommand(p);
   return in.ExecuteCommand(p);
}


// ----------------------------------------------------------------------------


bool Interpreter::IsNumberId(nodeType *p)  {
  return ((p->type==typeId) && (*(p->id.i)=='n'))||
         ((p->opr.oper=='@') && (*(p->opr.op[0]->id.i)=='n'));
}


// ----------------------------------------------------------------------------


bool Interpreter::IsBooleanId(nodeType *p)  {
  return ((p->type==typeId) && (*(p->id.i)=='b'))||
         ((p->opr.oper=='@') && (*(p->opr.op[0]->id.i)=='b'));
}


// ----------------------------------------------------------------------------


int Interpreter::ExecuteCommand(nodeType *p) {
    if (!p) { return -1; }
    if(p->type!=typeOpr)
      return -1;

    switch(p->opr.oper) {
      case CALL:      ExecuteProcedure(p);
                      return 0;

      case LIST:      m_ST.printSym();
                      return 0;

      case CLEAR:     m_ST.Clear();
                      ClearProgram();
                      return 0;

      case HALT:      m_ST.Clear();
                      ClearProgram();
                      cout << "--> Ending session" << endl << endl;
                      exit(0);

      case FOR:       ExecuteCommand(p->opr.op[0]);
                      while(ReadBoolean(p->opr.op[1]).GetGroundValue()) {
                         ExecuteCommand(p->opr.op[3]); 
                         ExecuteCommand(p->opr.op[2]);  
                      }
                      return 0;

      case WHILE:     while(ReadBoolean(p->opr.op[0]).GetGroundValue()) 
                         ExecuteCommand(p->opr.op[1]); 
                      return 0;

      case IF:        if (ReadBoolean(p->opr.op[0]).GetGroundValue())
                          return ExecuteCommand(p->opr.op[1]);
                      else { 
                        if (p->opr.nops > 2)
                          return ExecuteCommand(p->opr.op[2]);
                        else 
                          return 0;
                      }

      case MINIMIZE:  { 
                        if (pOptimizationConstraint==NULL) 
                          setLimits(p, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), false); 
                        else {
                          if (pOptimizationConstraint!=p) {
                            cout << "Only the first optimization constraint is considered " << endl;
                            return 0;
                          }
                        }
                        Number n((unsigned int)nOptimalCandidate);
                        m_ST.letInt(sOptimizationVarName, n);
                      }
                      return 0;

      case MAXIMIZE:  { 
                        if (pOptimizationConstraint==NULL) 
                          setLimits(p, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), true); 
                        else {
                          if (pOptimizationConstraint!=p) {
                            cout << "Only the first optimization constraint is considered " << endl;
                            return 0;
                          }
                        }
                        Number n((unsigned int)nOptimalCandidate);
                        m_ST.letInt(sOptimizationVarName, n);
                      }
                      return 0;

      case ASSERT:    Solve(p->opr.op[0], false);
                      return 0;

      case ASSERTA:   Solve(p->opr.op[0], true);
                      return 0;

      case PRINT:     if (IsNumberId(p->opr.op[0])) 
                         ReadNumber(p->opr.op[0]).print('d');
                      else 
                         ReadBoolean(p->opr.op[0]).print();
                      return 0;

      case PRINTX:    ReadNumber(p->opr.op[0]).print('x');
                      return 0;

      case PRINTB:    ReadNumber(p->opr.op[0]).print('b');
                      return 0;

      case ';':       ExecuteCommand(p->opr.op[0]); 
                      return ExecuteCommand(p->opr.op[1]);

      case '=':       if (p->opr.nops == 2) {
                        if (IsNumberId(p->opr.op[0]))  
                           m_ST.letInt(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]));
                        else
                          m_ST.letBool(p->opr.op[0]->id.i, ReadBoolean(p->opr.op[1])); 
                      }
                      else if (p->opr.nops == 3) {
                        if (IsNumberId(p->opr.op[0])) 
                          m_ST.letIntEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2])); 
                        else
                          m_ST.letBoolEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadBoolean(p->opr.op[2])); 
                      } 
                      else {
                        if (IsNumberId(p->opr.op[0])) 
                          m_ST.letIntEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), ReadNumber(p->opr.op[3])); 
                        else
                          m_ST.letBoolEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), ReadBoolean(p->opr.op[3])); 
                      }
                      return 0;

      case PLUSPLUS:
                      { 
                        Number n((unsigned int)0); Number nleft((unsigned int)0); Number nright((unsigned int)0);
                        if (p->opr.nops == 1) 
                          nleft = m_ST.getIntValue(p->opr.op[0]->id.i,&iVarCounter); 
                        else if (p->opr.nops == 2) 
                          nleft = m_ST.getIntElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),&iVarCounter); 
                        else 
                          nleft = m_ST.getIntElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]),&iVarCounter); 
                        nright=1;
                        n = nleft + nright; 
                        if (p->opr.nops == 1) 
                          m_ST.letInt(p->opr.op[0]->id.i, n); 
                        else if (p->opr.nops == 2) 
                          m_ST.letIntEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), n); 
                        else 
                          m_ST.letIntEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), n); 
                      }
                      return 0;

      case MINUSMINUS:
                      { 
                        Number n((unsigned int)0); Number nleft((unsigned int)0); Number nright((unsigned int)0);
                        if (p->opr.nops == 1) 
                          nleft = m_ST.getIntValue(p->opr.op[0]->id.i,&iVarCounter); 
                        else if (p->opr.nops == 2) 
                          nleft = m_ST.getIntElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),&iVarCounter); 
                        else 
                          nleft = m_ST.getIntElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]),&iVarCounter); 
                        nright=1;
                        n = nleft - nright; 
                        if (p->opr.nops == 1) 
                          m_ST.letInt(p->opr.op[0]->id.i, n); 
                        else if (p->opr.nops == 2) 
                          m_ST.letIntEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), n); 
                        else 
                          m_ST.letIntEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), n); 
                      }
                      return 0;

      case PLUSEQ: 
      case MINUSEQ:
      case MULTEQ:
      case BITWISEANDEQ: 
      case BITWISEOREQ: 
      case BITWISEXOREQ:
      case LSHIFTEQ:  
      case RSHIFTEQ:    
                   {     
                      Number n((unsigned int)0); Number nleft((unsigned int)0);
                      Boolean b(false),bleft(false);
                      if (p->opr.nops == 2) 
                          nleft=m_ST.getIntValue(p->opr.op[0]->id.i,&iVarCounter); 
                      else if (p->opr.nops == 3) 
                          nleft = m_ST.getIntElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),&iVarCounter); 
                      else 
                          nleft = m_ST.getIntElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]),&iVarCounter); 

                      switch(p->opr.oper) {
                        case PLUSEQ:   n = nleft + ReadNumber(p->opr.op[p->opr.nops-1]); break;
                        case MINUSEQ:  n = nleft - ReadNumber(p->opr.op[p->opr.nops-1]); break;
                        case MULTEQ:   n = nleft * ReadNumber(p->opr.op[p->opr.nops-1]); break;
//                      case DIVEQ:    n = nleft + ReadNumber(p->opr.op[p->opr.nops-1]); break;
                        case BITWISEANDEQ:  n = nleft & ReadNumber(p->opr.op[p->opr.nops-1]); break;
                        case BITWISEOREQ:   n = nleft | ReadNumber(p->opr.op[p->opr.nops-1]); break;
                        case BITWISEXOREQ:  n = nleft ^ ReadNumber(p->opr.op[p->opr.nops-1]); break;
                        case LSHIFTEQ: n = nleft << ReadNumber(p->opr.op[p->opr.nops-1]); break;
                        case RSHIFTEQ: n = nleft >> ReadNumber(p->opr.op[p->opr.nops-1]); break;
                      }

                      if (p->opr.nops == 2) 
                          m_ST.letInt(p->opr.op[0]->id.i, n); 
                      else if (p->opr.nops == 3) 
                          m_ST.letIntEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), n); 
                      else 
                          m_ST.letIntEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), n); 
              
                      return 0;
                   }

      case ANDEQ: 
      case OREQ: 
      case XOREQ:
                   {     
                      Number n((unsigned int)0); Number nleft((unsigned int)0);
                      Boolean b(false),bleft(false);
                      if (p->opr.nops == 2) 
                          bleft=m_ST.getBoolValue(p->opr.op[0]->id.i,&iVarCounter); 
                      else if (p->opr.nops == 3) 
                          bleft = m_ST.getBoolElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),&iVarCounter); 
                      else 
                          bleft = m_ST.getBoolElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]),&iVarCounter); 

                      switch(p->opr.oper) {
                        case ANDEQ:   b = bleft & ReadBoolean(p->opr.op[p->opr.nops-1]); break;
                        case OREQ:    b = bleft | ReadBoolean(p->opr.op[p->opr.nops-1]); break;
                        case XOREQ:   b = bleft ^ ReadBoolean(p->opr.op[p->opr.nops-1]); break;
                      }

 	                    if (p->opr.nops == 2) 
                        m_ST.letBool(p->opr.op[0]->id.i, b); 
                      else if (p->opr.nops == 3) 
                        m_ST.letBoolEl(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), b); 
     	                else 
                        m_ST.letBoolEl2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]), b); 
              
                      return 0;
                   }

       default: break;
    }

    return 0;
}


// ----------------------------------------------------------------------------


Number Interpreter::ReadNumber(nodeType *p) {
    if (!p) { Number n((unsigned int)0); return n; }   
    switch(p->type) {

    case typeIntConst:  { Number n(p->intConst.value); 
                        return n; 
                        }

    case typeId:        return m_ST.getIntValue(p->id.i,&iVarCounter);

    case typeOpr:
        switch(p->opr.oper) {
        case '@':       if(p->opr.nops==2) {
                          return m_ST.getIntElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),&iVarCounter); 
                        }
                        else 
                          return m_ST.getIntElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]),&iVarCounter); 
        case UMINUS:    return ReadNumber(p->opr.op[0]).negate();
        case '+':       return ReadNumber(p->opr.op[0]) + ReadNumber(p->opr.op[1]);
        case '-':       return ReadNumber(p->opr.op[0]) - ReadNumber(p->opr.op[1]);
        case '*':       return ReadNumber(p->opr.op[0]) * ReadNumber(p->opr.op[1]);
//        case '/':       return ReadNumber(p->opr.op[0]) / ReadNumber(p->opr.op[1]);
        case '&':       return ReadNumber(p->opr.op[0]) & ReadNumber(p->opr.op[1]);
        case '|':       return ReadNumber(p->opr.op[0]) | ReadNumber(p->opr.op[1]);
        case '^':       return ReadNumber(p->opr.op[0]) ^ ReadNumber(p->opr.op[1]);
        case '~':       return ReadNumber(p->opr.op[0]).bitnegate();
        case LSHIFT:    return ReadNumber(p->opr.op[0]) << ReadNumber(p->opr.op[1]);
        case RSHIFT:    return ReadNumber(p->opr.op[0]) >> ReadNumber(p->opr.op[1]);
        case ITE:       return ReadNumber(p->opr.op[1]).ite(ReadBoolean(p->opr.op[0]),ReadNumber(p->opr.op[2]));
        case BOOL2NUM:  return ReadBoolean(p->opr.op[0]).Int();
        case SGN:       return ReadNumber(p->opr.op[0]).sgn();
        }
    default: break;
    }
    return (unsigned int)0;
}


// ----------------------------------------------------------------------------


Boolean Interpreter::ReadBoolean(nodeType *p) {
    if (!p) { Boolean b(false); return b; } 
    switch(p->type) {
    case typeBoolConst: { Boolean b(p->boolConst.value); return b; };
    case typeId:        return m_ST.getBoolValue(p->id.i,&iVarCounter);
    case typeOpr:
        switch(p->opr.oper) {
        case '@':       if(p->opr.nops==2)
                          return m_ST.getBoolElValue(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]),&iVarCounter); 
                        else 
                          return m_ST.getBoolElValue2(p->opr.op[0]->id.i, ReadNumber(p->opr.op[1]), ReadNumber(p->opr.op[2]),&iVarCounter); 
        case LOGICALAND:return ReadBoolean(p->opr.op[0]) & ReadBoolean(p->opr.op[1]);
        case LOGICALOR: return ReadBoolean(p->opr.op[0]) | ReadBoolean(p->opr.op[1]);
        case LOGICALXOR:return ReadBoolean(p->opr.op[0]) ^ ReadBoolean(p->opr.op[1]);
        case '!':       return ReadBoolean(p->opr.op[0]).negate();
        case '<':       return ReadNumber(p->opr.op[0]) < ReadNumber(p->opr.op[1]);        
        case '>':       return ReadNumber(p->opr.op[0]) > ReadNumber(p->opr.op[1]);   
        case  LE:       return ReadNumber(p->opr.op[0]) <= ReadNumber(p->opr.op[1]);   
        case  GE:       return ReadNumber(p->opr.op[0]) >= ReadNumber(p->opr.op[1]);   
        case  EQ:       return ReadNumber(p->opr.op[0]) == ReadNumber(p->opr.op[1]);   
        case  NE:       return ReadNumber(p->opr.op[0]) != ReadNumber(p->opr.op[1]);   
        case ITE:       return ReadBoolean(p->opr.op[1]).ite(ReadBoolean(p->opr.op[0]),ReadBoolean(p->opr.op[2]));
        case NUM2BOOL:  return ReadNumber(p->opr.op[0]).Bool();
        }
    default: break;  
    }
    return false;
}  


// ----------------------------------------------------------------------------


bool Interpreter::Solve(nodeType *p, bool bAllSolutions) {
   if (pOptimizationConstraint) 
     SolveOptimizationProblem(p);
   else
     SolveConstraint(p, bAllSolutions);
   return true;
}



// ----------------------------------------------------------------------------


void Interpreter::setLimits(nodeType *p, const Number& min, const Number& max, bool bMax) {
   assert ( min.IsGroundNumber() );
   assert ( max.IsGroundNumber() );
   sOptimizationVarName = p->opr.op[0]->id.i;
   nMin=min.GetGroundValueUnsigned();
   nMax=max.GetGroundValueUnsigned();
   bMaximize = bMax;
   nOptimalCandidate = bMax ? nMax : nMin;
   pOptimizationConstraint=p;
}


// ----------------------------------------------------------------------------


void Interpreter::SolveOptimizationProblem(nodeType *p) {
   CTimer Timer;
   Timer.StartMeasuringTime();

        unsigned best = 0;
        while(nMin <= nMax)  {
            cout << "Testing the value " << nMin << " for the variable " << sOptimizationVarName << " ..." << endl;
            if(SolveConstraint(p,false)) {
                best = bMaximize ? nMax : nMin;
                break;
            }
            else {
                if (bMaximize) 
                    nMax /= 2;
                else
                    nMin *= 2;
                m_ST.Clear();
                FormulaFactory::Instance()->Clear();
                nOptimalCandidate = bMaximize ? nMax : nMin;
                m_ST.letInt(sOptimizationVarName, nOptimalCandidate);
                vector<nodeType *>::iterator i;
                for(i=URSAprogram.begin();i+1!=URSAprogram.end();i++)  //execute until the assert command
                    ExecuteCommand(*i); 
            }
        }

        if (bMaximize) {
            nMin = best; nMax = best*2-1;
        }   
        else { 
            nMin = best/2+1; nMax = best;
        }   

        while(best && nMin <= nMax && ((nMin != best && !bMaximize)||(nMax != best && bMaximize)))  {
            m_ST.Clear();
            FormulaFactory::Instance()->Clear();
            nOptimalCandidate = (nMin+nMax)/2;
            m_ST.letInt(sOptimizationVarName, nOptimalCandidate);
            cout << "Testing the value " << nOptimalCandidate << " for the variable " << sOptimizationVarName << " ..." << endl;
            vector<nodeType *>::iterator i;
            for(i=URSAprogram.begin();i+1!=URSAprogram.end();i++)  //execute until the assert command
                ExecuteCommand(*i); 
            if(SolveConstraint(p,false)) {
                best = nOptimalCandidate;
                nMax = nOptimalCandidate-1;
            }
            else {
                nMin = nOptimalCandidate+1;
            }
        }
        cout << endl;
        if (best > 0) {
            cout << "Best found proof of the length " << best << endl;
            m_ST.Clear();
            FormulaFactory::Instance()->Clear();
            nOptimalCandidate = best;
            m_ST.letInt(sOptimizationVarName, best);
            vector<nodeType *>::iterator i;
            for(i=URSAprogram.begin();i+1!=URSAprogram.end();i++)  //execute until the assert command
                 ExecuteCommand(*i); 
        }
    

   /* binary search */
   while(1 && false) {
     cout << "Testing the value " << (nMin + nMax)/2 << " for the variable " << sOptimizationVarName << " ..." << endl;
     if(SolveConstraint(p,false)) {
       if(nMax<=nMin) {
         if (bMaximize)
           cout << "Maximal value for the variable " << sOptimizationVarName << " is " << (nMin + nMax)/2 << endl;
         else 
           cout << "Minimal value for the variable " << sOptimizationVarName << " is " << (nMin + nMax)/2 << endl;
         break; 
       }
       else {
         m_ST.Clear();
         FormulaFactory::Instance()->Clear();

         if (bMaximize)
           nMin = (nMin + nMax + 1)/2;
         else 
           nMax = (nMin + nMax)/2;
 
         Number n((unsigned int)((nMin+nMax)/2));
         m_ST.letInt(sOptimizationVarName, n);

         vector<nodeType *>::iterator i;
         for(i=URSAprogram.begin();i+1!=URSAprogram.end();i++)  //execute until the assert command
           ExecuteCommand(*i); 
       }
     }
     else {
       if(nMax<=nMin) {
         cout << "The constraint is not met for any admissible value for the variable " << sOptimizationVarName << endl;
         break;
       }

       m_ST.Clear();
       FormulaFactory::Instance()->Clear();

       if (bMaximize)
         nMax = (nMin + nMax)/2;
       else 
         nMin = (nMin + nMax + 1)/2;

       Number n((unsigned int)((nMin+nMax)/2));
       m_ST.letInt(sOptimizationVarName, n);

       vector<nodeType *>::iterator i;
       for(i=URSAprogram.begin();i+1!=URSAprogram.end();i++)  //execute until the assert command
         ExecuteCommand(*i); 
     }
   } 

   
   /* sequential search 
   while(1) {
     cout << "Testing the value " << (bMaximize ? nMax : nMin) << " for the variable " << sOptimizationVarName << " ..." << endl;
     if(SolveConstraint(p,false)) {
       if (bMaximize)
         cout << "Maximal value for the variable " << sOptimizationVarName << " is " << nMax << endl;
       else 
         cout << "Minimal value for the variable " << sOptimizationVarName << " is " << nMin << endl;
       break; 
     }
     else {
       if(nMax<=nMin) {
         cout << "The constraint is not met for any admissible value for the variable " << sOptimizationVarName << endl;
         break;
       }

       m_ST.Clear();
       FormulaFactory::Instance()->Clear();

       if (bMaximize) {
         nMax--;
         Number n((unsigned int)nMax);
         m_ST.letInt(sOptimizationVarName, n);
       }
       else {
         nMin++;
         Number n((unsigned int)nMin);
         m_ST.letInt(sOptimizationVarName, n);
       }

       vector<nodeType *>::iterator i;
       for(i=URSAprogram.begin();i+1!=URSAprogram.end();i++)  //execute until the assert command
         ExecuteCommand(*i); 
     }
   } */

   cout << "[Total time elapsed: " << Timer.ElapsedTime() << "]" << endl << endl;
}


// ----------------------------------------------------------------------------


bool Interpreter::SolveConstraint(nodeType *p, bool bAllSolutions) {
  
  double dTime_generation, dTime_solving;
  double dTime_parsing = m_Timer.ElapsedTime();
  m_Timer.StartMeasuringTime();

  Boolean bConstraint(true);

  bool bMoreConstraints = true;
  while(bMoreConstraints)  {
    if(p->opr.oper==';') {
      bConstraint = bConstraint & ReadBoolean(p->opr.op[1]); 
      bMoreConstraints = (p->opr.oper==';');
      p = p->opr.op[0];
    }
    else {
      bConstraint = bConstraint & ReadBoolean(p);
      bMoreConstraints=false;
    }
  }

  // cout << "--> constraint: " << endl;
  // bConstraint.print();

   Formula* root=bConstraint.GetAbstractValue().getFormulaFrom(0);
   if (root->GetType() == CONST) {
       if (((FormulaConst*)root)->GetValue() == true) {
        cout << "yes" << endl;
        return true;
      }
      else
      {
        cout << "no" << endl;
        cout << "[Number of solutions: 0]" << endl;
        return false;
      }
   }

  if (bConstraint.IsGroundBoolean()) { 
      if (bConstraint.GetGroundValue()==true) {
      cout << "yes" << endl;
      return true;
    }
    else {
      cout << "no" << endl;
      cout << "[Number of solutions: 0]" << endl;
      return false;
    }
  }

  bConstraint.SetConstraint(true);
  int varCount = iVarCounter;

  FormulaFactory::Instance()->SetIds(&varCount);
  vector< vector<int> > conj;
  int numOfModels = 0;
   
  unsigned int* pMappedVarId = new unsigned int[varCount+1];

  if(bMapping) {
    m_ST.printMapping();
  }
 
  if (!FormulaFactory::Instance()->GenerateCNF(bConstraint.GetAbstractValue(), conj, pMappedVarId, &varCount))  {
    cout << endl << "No solutions found" << endl;
    dTime_generation = m_Timer.ElapsedTime();
    m_Timer.StartMeasuringTime();
  } 
  else
  { 
    if(bDimacsOnly) {
      dTime_generation = m_Timer.ElapsedTime();
      m_Timer.StartMeasuringTime();

      cout << "p cnf " << varCount << " " << conj.size() << " " << endl;
      for (vector<vector<int> >::iterator iter = conj.begin(); iter != conj.end(); iter++) {
        for (vector<int>::iterator it = iter->begin(); it != iter->end(); it++) 
          cout << *it << " ";                              
        cout << "0" << endl;
      }
    }
    else  {

      SATsolver *pSolver;
      if (URSASolver == eArgoSAT)
        pSolver = new ArgoSATsolver;
      else if (URSASolver == eMiniSAT)
        pSolver = new MiniSATsolver;
      else
        pSolver = new ClaspSATsolver;

      pSolver->InitSolver(varCount);
        
      for (vector<vector<int> >::iterator iter = conj.begin(); iter != conj.end(); iter++) 
        pSolver->addClause(*iter);

      dTime_generation = m_Timer.ElapsedTime();
      m_Timer.StartMeasuringTime();

      while(true) {
        if (!pSolver->solve()) {
          if(!numOfModels)
            cout << "No solutions found" << endl;
          break;
        }

        ++numOfModels;
        if (bAllSolutions)
          cout << "--> Solution " << numOfModels << endl;

        if (!bQuiet) 
          m_ST.printIndependentMapped(pSolver, pMappedVarId);

        if (bCoherentLogicProofExport) 
            m_ST.exportCoherentLogicProof2Txt(pSolver, pMappedVarId);

        cout << endl;

        if (!bAllSolutions)
          break;
      }
      delete pSolver;
    }
  }
 
  delete [] pMappedVarId;

  dTime_solving = m_Timer.ElapsedTime();
  cout << "[Formula generation: " << dTime_parsing << "s; conversion to CNF: "<< dTime_generation << "s; total: " << dTime_parsing+dTime_generation << "s]" << endl; 
  cout << "[Solving time: " << dTime_solving << "s]" << endl;
  cout << "[Formula size: " << varCount << " variables, " << conj.size() << " clauses]" << endl << endl;

  return (numOfModels>0);
}


// ----------------------------------------------------------------------------


void Interpreter::RecordProcedure(nodeType *p) {
   // cout << "Procedure " << p->opr.op[0]->id.i << endl;
   URSAprocedures[p->opr.op[0]->id.i]=p;
}

// ----------------------------------------------------------------------------


void Interpreter::RecordCommand(nodeType *p) {
   URSAprogram.push_back(p);
}


// ----------------------------------------------------------------------------


void Interpreter::ClearCommand(nodeType *p) {
    int i;

    if (!p) return;
    if(p->type==typeId) 
      free(p->id.i);

    if (p->type == typeOpr) 
      for (i = 0; i < p->opr.nops; i++) 
        ClearCommand(p->opr.op[i]);

    free(p);
}


// ----------------------------------------------------------------------------


void Interpreter::ClearProgram() {
   vector<nodeType *>::iterator i;
   for(i=URSAprogram.begin();i!=URSAprogram.end();i++)
     ClearCommand(*i);
   URSAprogram.clear();

   pOptimizationConstraint=NULL;
}



// ----------------------------------------------------------------------------


void Interpreter::PrintProcedure() {
   map<const string, nodeType *, lstr >::iterator i;
   for(i=URSAprocedures.begin();i!=URSAprocedures.end();i++)
     PrintCommand(i->second);

}


// ----------------------------------------------------------------------------


void Interpreter::PrintCommand(nodeType *p) {
    int i;

    if (!p) return;
    if(p->type==typeId) 
      cout << p->id.i << " ";

    if(p->type==typeIntConst) 
      cout << p->intConst.value << " ";

    if(p->type==typeBoolConst) 
      cout << p->boolConst.value << " ";

    if (p->type == typeOpr) 
      for (i = 0; i < p->opr.nops; i++) 
        PrintCommand(p->opr.op[i]);
    cout << endl;
}



// ----------------------------------------------------------------------------


bool Interpreter::ExecuteProcedure(nodeType *p) {
   map<const string, nodeType *, lstr >::iterator it;
   it=URSAprocedures.find(p->opr.op[0]->id.i);
   if(it == URSAprocedures.end())  {
      cout << "Procedure " << p->opr.op[0]->id.i << "not defined" << endl; 
      return false;
   }      
//   cout << "Procedure " << p->opr.op[0]->id.i << " called " << endl; 

   // Copying arguments 
   nodeType *pargs=p->opr.op[1];
   nodeType *pdefargs=it->second->opr.op[1];
   Interpreter procInter;
   bool bMoreArgs = true;
   
   int actualargs=0;
   while(pargs->opr.oper==';')  {
     pargs = pargs->opr.op[0];
     actualargs++; 
   }  

   int defargs=0;
   while(pdefargs->opr.oper==';')  {
     pdefargs = pdefargs->opr.op[0];
     defargs++; 
   }  

   if(actualargs!=defargs) {
     cout << "Wrong number of arguments in the call of the procedure " << p->opr.op[0]->id.i << endl; 
     exit(1);
   }

   pargs=p->opr.op[1];
   pdefargs=it->second->opr.op[1];

   while(bMoreArgs)  {
     if(pargs->opr.oper==';') {
       if(IsNumberId(pdefargs->opr.op[1]))  {

         if (!IsNumberId(pargs->opr.op[1]) || m_ST.DefinedIntVar(pargs->opr.op[1]->id.i))
             procInter.m_ST.letInt(pdefargs->opr.op[1]->id.i, ReadNumber(pargs->opr.op[1]));
/*         else 
             procInter.m_ST.letInt(pdefargs->opr.op[1]->id.i, Number((unsigned int)0));*/
          procInter.m_ST.SetAccessedIntVar(pdefargs->opr.op[1]->id.i,false);
         
       }
       else {
         if (!IsBooleanId(pargs->opr.op[1]) || m_ST.DefinedBoolVar(pargs->opr.op[1]->id.i))
           procInter.m_ST.letBool(pdefargs->opr.op[1]->id.i, ReadBoolean(pargs->opr.op[1]));
/*         else 
           procInter.m_ST.letBool(pdefargs->opr.op[1]->id.i, Boolean(false));*/
         procInter.m_ST.SetAccessedBoolVar(pdefargs->opr.op[1]->id.i,false);
       }
       bMoreArgs = (pargs->opr.oper==';');
       pargs = pargs->opr.op[0];
       pdefargs = pdefargs->opr.op[0];
     }
     else {
       if(IsNumberId(pdefargs))  {

         if (!IsNumberId(pargs) || m_ST.DefinedIntVar(pargs->id.i)) 
           procInter.m_ST.letInt(pdefargs->id.i, ReadNumber(pargs));
/*         else 
           procInter.m_ST.letInt(pdefargs->id.i, Number((unsigned int)0));*/
         procInter.m_ST.SetAccessedIntVar(pdefargs->id.i,false);
       }
       else {
         if (!IsBooleanId(pargs) || m_ST.DefinedBoolVar(pargs->id.i))
           procInter.m_ST.letBool(pdefargs->id.i, ReadBoolean(pargs));
 /*        else
           procInter.m_ST.letBool(pdefargs->id.i, Boolean(false));*/
         procInter.m_ST.SetAccessedBoolVar(pdefargs->id.i,false);
       }

       bMoreArgs=false;
     }
   }

   // Executing procedure
   nodeType *pcode=it->second->opr.op[2];
   procInter.ExecuteCommandTree(pcode);

   // Copying arguments back
   pargs=p->opr.op[1];
   pdefargs=it->second->opr.op[1];

   bMoreArgs = true;
   while(bMoreArgs)  {
     if(pargs->opr.oper==';') {      
       if(IsNumberId(pargs->opr.op[1]))  {

         if (!procInter.m_ST.GetAccessedIntVar(pdefargs->opr.op[1]->id.i) && !m_ST.DefinedIntVar(pargs->opr.op[1]->id.i))
             procInter.m_ST.letInt(pdefargs->opr.op[1]->id.i, Number((unsigned int)0));

         m_ST.letInt(pargs->opr.op[1]->id.i, procInter.ReadNumber(pdefargs->opr.op[1]));
       }
       else 
         if(IsBooleanId(pargs->opr.op[1])) {

          if (!procInter.m_ST.GetAccessedBoolVar(pdefargs->opr.op[1]->id.i) && !m_ST.DefinedBoolVar(pargs->opr.op[1]->id.i))
             procInter.m_ST.letBool(pdefargs->opr.op[1]->id.i, Boolean(false));

          m_ST.letBool(pargs->opr.op[1]->id.i, procInter.ReadBoolean(pdefargs->opr.op[1]));

       }

       bMoreArgs = (pargs->opr.oper==';');
       pargs = pargs->opr.op[0];
       pdefargs = pdefargs->opr.op[0];
     }
     else {
       if(IsNumberId(pargs))  {

         if (!procInter.m_ST.GetAccessedIntVar(pdefargs->id.i) && !m_ST.DefinedIntVar(pargs->id.i))
             procInter.m_ST.letInt(pdefargs->id.i, Number((unsigned int)0));

         m_ST.letInt(pargs->id.i, procInter.ReadNumber(pdefargs));

       }
       else  
         if(IsBooleanId(pargs)) {

           if (!procInter.m_ST.GetAccessedBoolVar(pdefargs->id.i) && !m_ST.DefinedBoolVar(pargs->id.i))
              procInter.m_ST.letBool(pdefargs->id.i, Boolean(false));

            m_ST.letBool(pargs->id.i, procInter.ReadBoolean(pdefargs));
         }

       bMoreArgs=false;
     }
   }

   return true;
}


// ----------------------------------------------------------------------------


void Interpreter::ExecuteCommandTree(nodeType *p) {
     if(p->opr.oper==';') {
       ExecuteCommandTree(p->opr.op[0]);
       ExecuteCommand(p->opr.op[1]);
     }       
     else  
       ExecuteCommand(p);
}


// ----------------------------------------------------------------------------


int store_procedure(nodeType *p) {
   if (bSMTMode)
      smtIn.RecordProcedure(p);
   else
      in.RecordProcedure(p);
   return 0;
}



// ----------------------------------------------------------------------------



